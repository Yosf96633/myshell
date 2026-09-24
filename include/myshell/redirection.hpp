#pragma once

#include "myshell/parsed_command.hpp"

#include <string>
#include <vector>

struct DescriptorBackup {
    int target_fd = -1;
    int backup_fd = -1;
    int original_flags = 0;
    bool was_open = false;
};

// Apply redirections in order. This is intended for a child that can simply
// exit if a redirection fails.
bool apply_redirections(
    const std::vector<Redirection>& redirections,
    std::string& error);

// Apply redirections after saving every affected descriptor. If applying any
// operation fails, the original descriptor state is restored automatically.
bool apply_redirections_transactionally(
    const std::vector<Redirection>& redirections,
    std::vector<DescriptorBackup>& backups,
    std::string& error);

void restore_descriptors(std::vector<DescriptorBackup>& backups);
void discard_descriptor_backups(std::vector<DescriptorBackup>& backups);
