// #pragma once
// #include <string>
// #include <algorithm>
// #include <sys/stat.h>


// inline std::string sanitize_path(const std::string &root, const std::string &path) {
// // remove query part
// size_t q = path.find('?');
// std::string p = (q==std::string::npos) ? path : path.substr(0,q);
// // simple normalization: remove ".." and leading '/'
// std::string out;
// size_t i=0;
// while(i < p.size() && p[i]=='/') ++i;
// while(i < p.size()) {
// size_t j = p.find('/', i);
// std::string token = (j==std::string::npos) ? p.substr(i) : p.substr(i, j-i);
// if(token == "..") {
// // remove last segment
// size_t pos = out.find_last_of('/');
// if(pos==std::string::npos) out.clear(); else out.erase(pos);
// } else if(token.size() && token != ".") {
// if(!out.empty()) out.push_back('/');
// out += token;
// }
// if(j==std::string::npos) break; else i = j+1;
// }
// if(out.empty()) out = "index.html";
// std::string full = root + "/" + out;
// return full;
// }


// inline bool file_exists(const std::string &path) {
// struct stat st;
// return stat(path.c_str(), &st)==0 && S_ISREG(st.st_mode);
// }


// inline off_t file_size(const std::string &path) {
// struct stat st;
// if(stat(path.c_str(), &st)==0) return st.st_size;
// return -1;
// }
#pragma once
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>

// sanitize path: prevents path traversal and enforces root prefix
inline std::string sanitize_path(const std::string &root, const std::string &uri) {
    std::string u = uri;
    // remove query string
    auto qpos = u.find('?');
    if (qpos != std::string::npos) u.erase(qpos);
    // decode percent-encoding (simple)
    std::string decoded;
    for (size_t i=0;i<u.size();++i) {
        if (u[i] == '%' && i+2 < u.size()) {
            std::string hex = u.substr(i+1,2);
            char c = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            decoded.push_back(c);
            i += 2;
        } else decoded.push_back(u[i]);
    }
    // strip leading '/'
    if (!decoded.empty() && decoded.front() == '/') decoded.erase(0,1);

    // prevent traversal
    while (decoded.find("..") != std::string::npos) {
        auto pos = decoded.find("..");
        // if isolated ".." or part of segment -> remove
        if (pos == 0 || decoded[pos-1] == '/' || (pos+2 < decoded.size() && decoded[pos+2] == '/')) {
            decoded.erase(pos, 2);
        } else break;
    }
    // final join
    std::string final = root + "/" + decoded;
    // normalize double slashes
    std::string out;
    for (size_t i=0;i<final.size();++i) {
        if (i+1<final.size() && final[i]=='/' && final[i+1]=='/') continue;
        out.push_back(final[i]);
    }
    return out;
}

inline bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
inline off_t file_size(const std::string &p) {
    struct stat st;
    if (stat(p.c_str(), &st) == 0) return st.st_size;
    return -1;
}
inline std::string basename_from_path(const std::string &p) {
    auto pos = p.find_last_of('/');
    if (pos==std::string::npos) return p;
    return p.substr(pos+1);
}
