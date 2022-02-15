//
//  CPPNumericStringHashCompare.hpp
//
//  Created by Switt Kongdachalert on 4/1/2565 BE.
//

#ifndef CPPNumericStringHashCompare_hpp
#define CPPNumericStringHashCompare_hpp

#include <stdio.h>
// String hash switch case implementation as seen here
// https://learnmoderncpp.com/2020/06/01/strings-as-switch-case-labels/
constexpr inline unsigned long long string_hash(const char *s) {
    unsigned long long hash{}, c{};
    for (auto p = s; *p; ++p, ++c) {
        hash += *p << c;
    }
    return hash;
}

constexpr inline unsigned long long operator"" _sh(const char *s, size_t) {
    return string_hash(s);
}

#endif /* CPPNumericStringHashCompare_hpp */
