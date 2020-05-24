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

/**
 * cliex.cpp
 *
 * only contains the main function.
 */

#include "cliex.hpp"
#include "args.hpp"
#include "screen.hpp"
#include "files.hpp"
#include "type_config.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <experimental/filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <menu.h>
#include <ncurses.h>
#include <pwd.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <variant>
#include <vector>

namespace fs = std::experimental::filesystem;
using std::literals::string_literals::operator""s;

cliex::type_config setup_type_config() noexcept;
void show_file_info(
    WINDOW *window,
    const cliex::file_info &file_info) noexcept;

int main(int argc, const char *argv[])
{
    cliex::cl::opts opts = cliex::cl::parse_argv(argc, argv);

    cliex::type_config type_config = setup_type_config();

    setlocale(LC_ALL, "");
    initscr();
    clear();
    noecho();
    curs_set(0);
    cbreak();
    nl();
    use_default_colors();
    keypad(stdscr, 1);

    start_color();
    init_pair(cliex::screen::color_pair_inaccessible_dir, COLOR_RED, -1);

    WINDOW *explorer_win = cliex::screen::create_win(EXPLORER_WIN_HEIGHT-1, EXPLORER_WIN_WIDTH, 2, 1, "");
    MENU *explorer_menu = nullptr;

    WINDOW *file_info_win = cliex::screen::create_win(PROPERTY_WIN_HEIGHT, PROPERTY_WIN_WIDTH, 1, EXPLORER_WIN_WIDTH + 2, "File Information");

    mvaddstr(1, 3, "CLI File Explorer");
    mvaddstr(LINES - 2, SUB_WIDTH + 7, ("Quit by pressing q."));

    std::vector<std::string> choices;
    std::vector<ITEM *> items;
    cliex::file_info selected_file_info;
    fs::path current_dir;

    std::function<const std::string &()> get_selected = [&]() -> const std::string & {
        return choices[item_index(current_item(explorer_menu))];
    };

    std::function<void(const fs::path &newdir)> change_dir = [&](const fs::path &newdir) {
        if (!fs::is_directory(newdir) ||
                selected_file_info.type != fs::file_type::directory ||
                !std::get<cliex::dir_info>(selected_file_info.extra_info).has_access) return;

        choices.clear();
        cliex::screen::clear_menu(explorer_menu, items);

        cliex::get_dir_content(choices, newdir, opts.show_hidden_files);
        std::sort(choices.begin(), choices.end(), [](const std::string &a, const std::string &b) {
            return a < b;
        });

        explorer_menu = cliex::screen::add_file_menu(explorer_win, choices, items, newdir, opts.max_columns);

        current_dir = newdir;
        chdir(current_dir.c_str());
    };

    {
        fs::path home_dir = cliex::get_home_dir();
        selected_file_info = cliex::get_file_info(home_dir, type_config);
        change_dir(home_dir);
    }

    for (bool running = true; running; ) {
        // show file info
        {
            fs::path tmp = current_dir / get_selected();
            // this is needed because directory items have a slash at the end of
            // their names. that should probably be changed TODO
            if (tmp.filename() == ".") tmp = tmp.parent_path();
            selected_file_info = cliex::get_file_info(tmp, type_config);
        }
        show_file_info(file_info_win, selected_file_info);

        // refresh screen
        refresh();
        box(explorer_win, 0, 0);
        wrefresh(explorer_win);
        box(file_info_win, 0, 0);
        wrefresh(file_info_win);

        // wait for input and handle
        switch (getch()) {
        case KEY_DOWN:
            menu_driver(explorer_menu, REQ_DOWN_ITEM);
            break;
        case KEY_UP:
            menu_driver(explorer_menu, REQ_UP_ITEM);
            break;
        case KEY_RIGHT:
            menu_driver(explorer_menu, REQ_RIGHT_ITEM);
            break;
        case KEY_LEFT:
            menu_driver(explorer_menu, REQ_LEFT_ITEM);
            break;
        case KEY_NPAGE:
            menu_driver(explorer_menu, REQ_SCR_DPAGE);
            break;
        case KEY_PPAGE:
            menu_driver(explorer_menu, REQ_SCR_UPAGE);
            break;
        case 'q':
            running = false;
            break;
        case '\n': {
            std::string selected = get_selected();
            if (selected == "..") {
                change_dir(current_dir.parent_path());
            }
            else if (*(selected.end()-1) == '/') {
                selected.erase(selected.end()-1);
                change_dir(current_dir / selected);
            }
            break;
        }
        case KEY_BACKSPACE:
            change_dir(current_dir.parent_path());
            break;
        }
    }

    cliex::screen::clear_menu(explorer_menu, items);
    delwin(file_info_win);
    delwin(explorer_win);
    endwin();

    return 0;
}

cliex::type_config setup_type_config() noexcept
{
    const fs::path default_type_config_path = cliex::get_root_path() / "etc"     / "cliex" / "default.cfg";
    const fs::path user_type_config_path    = cliex::get_home_dir()  / ".config" / "cliex" / "user.cfg";

    const bool default_type_config_available = fs::exists(default_type_config_path) && fs::is_regular_file(default_type_config_path);
    const bool user_type_config_available    = fs::exists(user_type_config_path)    && fs::is_regular_file(user_type_config_path);

    cliex::type_config default_type_config;
    if (default_type_config_available) {
        default_type_config = cliex::type_config::read_from(default_type_config_path);
    }
    cliex::type_config user_type_config;
    if (user_type_config_available) {
        user_type_config = cliex::type_config::read_from(user_type_config_path);
    }

    if (default_type_config != user_type_config) {
        user_type_config.merge_with(default_type_config);

        // only overwrite the user type config when it exists
        if (user_type_config_available) {
            std::ofstream user_type_config_stream(user_type_config_path);
            user_type_config_stream << "# Configuration file for cliex.\n"
                                    "# It is used by the file explorer to detect file types correctly.\n\n";
            user_type_config_stream << user_type_config;
        }
    }

    return user_type_config;
}

void show_file_info(
    WINDOW *window,
    const cliex::file_info &file_info) noexcept
{
    using std::make_pair;

    const std::array<std::string, 5> units {"Byte", "KB", "MB", "GB", "TB"};
    constexpr std::array<std::pair<int, int>, 6> window_info_positions {
        make_pair(3, 3),
        make_pair(4, 3),
        make_pair(6, 3),
        make_pair(7, 3),
        make_pair(8, 3),
        make_pair(10, 3)
    };

    // clear the lines were we show the infos
    for (const auto &p : window_info_positions) {
        wmove(window, p.first, p.second);
        wclrtoeol(window);
    }

    // file name
    std::string selected_file_name = file_info.name;
    mvwaddstr(window, 3, 3, selected_file_name.c_str());

    // file type
    std::string selected_file_type_desc = "Type: " + file_info.type_desc;
    mvwaddstr(window, 4, 3, selected_file_type_desc.c_str());

    // file permissions
    std::string selected_file_perms = "Permissions: " + cliex::perms_to_string(file_info.perms);
    mvwaddstr(window, 6, 3, selected_file_perms.c_str());

    // file size
    std::string selected_file_size = "Size: ";
    if (file_info.type == fs::file_type::regular) {
        cliex::regular_file_info regular_file_info = std::get<cliex::regular_file_info>(file_info.extra_info);

        // TODO use a double if over 1024
        uintmax_t size = regular_file_info.size;
        for (size_t i = 0; ; ++i) {
            if (size < 1024) {
                selected_file_size += std::to_string(size) + ' ' + units[i];
                break;
            }
            size /= 1024;
        }
    }
    else if (file_info.type == fs::file_type::directory) {
        cliex::dir_info dir_info = std::get<cliex::dir_info>(file_info.extra_info);

        if (dir_info.has_access) {
            selected_file_size += std::to_string(dir_info.subdirsc);
            if (dir_info.subdirsc == 1)
                selected_file_size += " subdirectory";
            else
                selected_file_size += " subdirectories";

            selected_file_size += ", " + std::to_string(dir_info.filesc);
            if (dir_info.filesc == 1)
                selected_file_size += " file";
            else
                selected_file_size += " files";
        }
        else {
            selected_file_size += "Unknown";
        }
    }
    else {
        selected_file_size += "N/A";
    }
    mvwaddstr(window, 7, 3, selected_file_size.c_str());

    // last write time
    // FIXME only `chrono::system_clock` has `to_time_t`
    //       this will fail on systems where `fs::file_time_type::clock` is not `chrono::system_clock`
    fs::file_time_type ftime = file_info.last_write_time;
    time_t cftime = fs::file_time_type::clock::to_time_t(ftime);
    std::string selected_file_last_write_time = "Last mod.: "s + std::asctime(std::localtime(&cftime));
    mvwaddstr(window, 8, 3, selected_file_last_write_time.c_str());

    // symlink target
    if (file_info.type == fs::file_type::symlink) {
        cliex::symlink_info symlink_info = std::get<cliex::symlink_info>(file_info.extra_info);

        std::string selected_file_symlink_target = "Symlink target: "s + symlink_info.target.string();
        mvwaddstr(window, 10, 3, selected_file_symlink_target.c_str());
    }

    box(window, 0, 0);
}
