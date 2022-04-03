#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <pwd.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <map>
#include <set>

#include <filesystem>

#define FUSE_USE_VERSION 30
#include <fuse.h>

namespace fs = std::filesystem;

fs::path get_path(const char* path, const std::set<fs::path>& filesystem) {
    fs::path abs_path;
    for (auto& dir : filesystem) {
        fs::path cur_path = dir / path;
        if (fs::exists(cur_path) && (abs_path.empty()
                    || fs::last_write_time(abs_path) < 
                       fs::last_write_time(cur_path)))
                abs_path = cur_path;
    }
    return abs_path;
}

std::set<fs::path> open_filesystem(const std::string& src) {
    // getting ~ home directory
    char* homedir;
    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }
    std::set<fs::path> filesystem;
    std::stringstream stream(src);
    for (std::string cur_dir; std::getline(stream, cur_dir, ':');) {
        fs::path abs_path;
        if (cur_dir[0] == '~') {
            abs_path = fs::canonical(std::string(homedir) + &cur_dir[1]);
        } else {
            abs_path = fs::canonical(cur_dir);
        }
        filesystem.insert(abs_path);
    }
    return filesystem;
}

int my_open(const char* path, struct fuse_file_info* fi) {
    if (path[0] == '/')
        ++path;
    std::set<fs::path>* filesystem = 
        reinterpret_cast<std::set<fs::path>*>(fuse_get_context()->private_data);

    fs::path abs_path = get_path(path, *filesystem);

    if (O_RDONLY != (fi->flags & O_ACCMODE)) {
        return -EACCES;
    }
    if (abs_path.empty()) {
        return -ENOENT;
    }
    return 0;
}

int my_stat(const char* path, struct stat* st, struct fuse_file_info* fi) {
    if (path[0] == '/')
        ++path;
    std::set<fs::path>* filesystem = 
        reinterpret_cast<std::set<fs::path>*>(fuse_get_context()->private_data);

    fs::path abs_path = get_path(path, *filesystem);
    if (abs_path.empty()) {
        return -ENOENT;
    }
    if (!fs::is_directory(abs_path)) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        st->st_size = fs::file_size(abs_path);
        return 0;
    }
   
    std::set<fs::path> paths;
    for (auto& dir : *filesystem) {
        fs::path cur_path = dir / path;
        if (fs::exists(cur_path)) 
            for (auto& cur_file : fs::recursive_directory_iterator(cur_path)) 
                if (!fs::is_directory(cur_file.path()))
                    paths.insert(cur_file.path().lexically_relative(cur_path));
    }
    st->st_nlink = paths.size(); 
    st->st_mode = 0555 | S_IFDIR;
    return 0;

}

int my_readdir(const char* path, void* out, fuse_fill_dir_t filler, off_t off,
               struct fuse_file_info* fi, enum fuse_readdir_flags flags) {   
    if (path[0] == '/')
        ++path;
    std::set<fs::path>* filesystem = 
        reinterpret_cast<std::set<fs::path>*>(fuse_get_context()->private_data);
    
    fs::path abs_path = get_path(path, *filesystem);

    if (abs_path.empty() || !fs::is_directory(abs_path))
        return -ENOENT;

    std::set<fs::path> paths;
    for (auto& dir : *filesystem) {
        fs::path cur_path = dir / path;
        if (fs::exists(cur_path)) 
            for (auto& cur_file : fs::directory_iterator(cur_path))
                paths.insert(cur_file.path().lexically_relative(cur_path));
    }

    filler(out, ".", NULL, 0,  fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
    filler(out, "..", NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
    for (auto& cur_path : paths)
        filler(out, cur_path.filename().c_str(), NULL, 0,
                               fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
    return 0;
}

int my_read(const char* path, char* out, size_t size, off_t off,
         struct fuse_file_info* fi) {
    if (path[0] == '/')
        ++path;
    std::set<fs::path>* filesystem = 
        reinterpret_cast<std::set<fs::path>*>(fuse_get_context()->private_data);
    
    fs::path abs_path = get_path(path, *filesystem);
    if (abs_path.empty())
        return -ENOENT;
    
    int file_size = fs::file_size(abs_path);
    
    if (off > file_size)
        return 0;
    
    const char* path_ptr = abs_path.c_str();
    std::fstream file_str(path_ptr);
    file_str.seekg(off);
    
    if (off+size > file_size)
        size = file_size - off;
    
    file_str.read(out, size);
    return size;
}

struct fuse_operations operations;

int main(int argc, char* argv[]) {
    operations.readdir    = my_readdir;
    operations.getattr    = my_stat;
    operations.open       = my_open;
    operations.read       = my_read;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    typedef struct {
        char* src;
    } my_options_t;
    my_options_t my_options;
    memset(&my_options, 0, sizeof(my_options));

    struct fuse_opt opt_specs[] = {
        {"--src %s", offsetof(my_options_t, src), 0},
        {NULL, 0, 0}
    };
    
    fuse_opt_parse(&args, &my_options, opt_specs, NULL);
    std::string src(my_options.src);

    if (!src.empty()) {
        auto fs_map = open_filesystem(src);
        
        int ret = fuse_main(
            args.argc, args.argv,
            &operations,
            &fs_map);

        return ret;
    }
}
