#ifndef ARACHNE_COMMON_H
#define ARACHNE_COMMON_H
namespace Arachne {
    const int CACHE_LINE_SIZE = 64;

    // A macro to disallow the copy constructor and operator= functions
    #ifndef DISALLOW_COPY_AND_ASSIGN
    #define DISALLOW_COPY_AND_ASSIGN(TypeName) \
        TypeName(const TypeName&) = delete;             \
        TypeName& operator=(const TypeName&) = delete;
    #endif
}

#endif
