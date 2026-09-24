#include "myshell/redirection.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <unordered_set>

using namespace std;

namespace {

bool backup_descriptors(
    const vector<Redirection>& redirections,
    vector<DescriptorBackup>& backups,
    string& error) {
    int largest_fd = STDERR_FILENO;
    for (const auto& redirection : redirections) {
        largest_fd = max(largest_fd, redirection.target_fd);
        largest_fd = max(largest_fd, redirection.source_fd);
    }
    if (largest_fd == numeric_limits<int>::max()) {
        error = "file descriptor is too large";
        return false;
    }
    const int minimum_backup_fd = largest_fd + 1;

    unordered_set<int> backed_up;
    for (const auto& redirection : redirections) {
        if (!backed_up.insert(redirection.target_fd).second) {
            continue;
        }

        DescriptorBackup backup;
        backup.target_fd = redirection.target_fd;
        errno = 0;
        backup.original_flags = fcntl(backup.target_fd, F_GETFD);
        if (backup.original_flags < 0) {
            if (errno != EBADF) {
                error = "cannot inspect file descriptor "
                    + to_string(backup.target_fd) + ": " + strerror(errno);
                discard_descriptor_backups(backups);
                return false;
            }
        } else {
            backup.was_open = true;
            backup.backup_fd = fcntl(
                backup.target_fd, F_DUPFD_CLOEXEC, minimum_backup_fd);
            if (backup.backup_fd < 0) {
                error = "cannot back up file descriptor "
                    + to_string(backup.target_fd) + ": " + strerror(errno);
                discard_descriptor_backups(backups);
                return false;
            }
        }
        backups.push_back(backup);
    }
    return true;
}

bool apply_one_redirection(const Redirection& redirection, string& error) {
    if (redirection.type == RedirectionType::close) {
        if (close(redirection.target_fd) != 0 && errno != EBADF) {
            error = "cannot close file descriptor "
                + to_string(redirection.target_fd) + ": " + strerror(errno);
            return false;
        }
        return true;
    }

    if (redirection.type == RedirectionType::duplicate) {
        if (dup2(redirection.source_fd, redirection.target_fd) < 0) {
            error = "cannot duplicate file descriptor "
                + to_string(redirection.source_fd) + " onto "
                + to_string(redirection.target_fd) + ": " + strerror(errno);
            return false;
        }
        return true;
    }

    int flags = O_RDONLY;
    if (redirection.type == RedirectionType::output) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (redirection.type == RedirectionType::append) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }

    const int opened_fd = open(redirection.path.c_str(), flags, 0666);
    if (opened_fd < 0) {
        error = redirection.path + ": " + strerror(errno);
        return false;
    }
    if (opened_fd != redirection.target_fd) {
        if (dup2(opened_fd, redirection.target_fd) < 0) {
            error = "cannot redirect file descriptor "
                + to_string(redirection.target_fd) + ": " + strerror(errno);
            close(opened_fd);
            return false;
        }
        close(opened_fd);
    }
    return true;
}

} // namespace

bool apply_redirections(
    const vector<Redirection>& redirections,
    string& error) {
    for (const auto& redirection : redirections) {
        if (!apply_one_redirection(redirection, error)) {
            return false;
        }
    }
    return true;
}

bool apply_redirections_transactionally(
    const vector<Redirection>& redirections,
    vector<DescriptorBackup>& backups,
    string& error) {
    if (!backup_descriptors(redirections, backups, error)) {
        return false;
    }
    if (!apply_redirections(redirections, error)) {
        restore_descriptors(backups);
        return false;
    }
    return true;
}

void restore_descriptors(vector<DescriptorBackup>& backups) {
    for (auto backup = backups.rbegin(); backup != backups.rend(); ++backup) {
        if (backup->was_open) {
            if (dup2(backup->backup_fd, backup->target_fd) >= 0) {
                fcntl(backup->target_fd, F_SETFD, backup->original_flags);
            }
        } else {
            close(backup->target_fd);
        }
    }
    discard_descriptor_backups(backups);
}

void discard_descriptor_backups(vector<DescriptorBackup>& backups) {
    for (const auto& backup : backups) {
        if (backup.backup_fd >= 0) {
            close(backup.backup_fd);
        }
    }
    backups.clear();
}
