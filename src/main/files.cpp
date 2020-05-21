/*
 * A simple terminal-based file explorer written in C++ using the ncurses lib.
 * Copyright (C) 2020  Andreas Sünder
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>
 */

#include "files.hpp"
#include "type_config.hpp"
#include <cstdlib>
#include <experimental/filesystem>
#include <functional>
#include <pwd.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::experimental::filesystem;
using cliex::file_info;
using cliex::type_config;
using fs::absolute;
using fs::current_path;
using fs::file_size;
using fs::is_block_file;
using fs::is_character_file;
using fs::is_directory;
using fs::is_fifo;
using fs::is_socket;
using fs::is_symlink;
using fs::last_write_time;
using fs::path;
using fs::perms;
using fs::status;
using std::function;
using std::string;

path cliex::get_root_path() noexcept
{
    return fs::absolute(fs::current_path()).root_path();
}

path cliex::get_home_dir() noexcept
{
    const string HOME = getenv("HOME");
    if(!HOME.empty()) return absolute(HOME);

    const string pw_dir = getpwuid(getuid())->pw_dir;
    if(!pw_dir.empty()) return absolute(pw_dir);

    return absolute(current_path());
}

file_info cliex::get_file_info(const path& path, const type_config& type_config) noexcept
{
    bool is_dir = is_directory(path);
    perms perms = status(path).permissions();

    string type_desc;

    if (is_dir) type_desc = "Directory";
    else if (is_symlink(path)) type_desc = "Symlink";
    else if (is_block_file(path)) type_desc = "Block Device";
    else if (is_character_file(path)) type_desc = "Character Device";
    else if (is_fifo(path)) type_desc = "Named IPC Pipe";
    else if (is_socket(path)) type_desc = "Named IPC Socket";
    else { // regular file
        bool executable = (perms & (perms::owner_exec | perms::group_exec | perms::others_exec)) != perms::none;
        if (executable) type_desc = "Executable";

        auto types = type_config.types();
        auto it_f = types.find(path.filename());
        auto it_e = types.find(path.extension());

        if (it_f != types.end()) {
            type_desc = it_f->second + " (" + type_desc + ')';
        }
        else if (it_e != types.end()) {
            type_desc = it_f->second + " (" + type_desc + ')';
        }
        else if(type_desc.empty()) {
            type_desc = "Unknown";
        }
    }

    return file_info {
        .name = path.filename(),
        .type_desc = type_desc,
        .size = (is_dir ? 0 : file_size(path)),
        .perms = perms,
        .last_write_time = last_write_time(path)
    };
}

string cliex::perms_to_string(perms perms) noexcept
{
    string str;

    function<void(fs::perms, char)> tmp = [&](fs::perms test_perms, char c) {
        str += (((perms & test_perms) == test_perms) ? c : '-');
    };

    tmp(perms::owner_read, 'r');
    tmp(perms::owner_write, 'w');
    tmp(perms::owner_exec, 'x');

    tmp(perms::group_read, 'r');
    tmp(perms::group_write, 'w');
    tmp(perms::group_exec, 'x');

    tmp(perms::others_read, 'r');
    tmp(perms::others_write, 'w');
    tmp(perms::others_exec, 'x');

    return str;
}
