#ifndef _PATTERNFIND_H
#define _PATTERNFIND_H

#include <vector>
#include <string>

struct PatternByte
{
    unsigned char data;
    unsigned char mask;
};

//returns: offset to data when found, -1 when not found
size_t patternfind(
    const unsigned char* data, //data
    size_t datasize, //size of data
    const char* pattern, //pattern to search
    int* patternsize = 0 //outputs the number of bytes the pattern is
);

//returns: nothing
void patternwrite(
    unsigned char* data, //data
    size_t datasize, //size of data
    const char* pattern //pattern to write
);

//returns: true on success, false on failure
bool patternsnr(
    unsigned char* data, //data
    size_t datasize, //size of data
    const char* searchpattern, //pattern to search
    const char* replacepattern //pattern to write
);

//returns: true on success, false on failure
bool patterntransform(const std::string & patterntext, //pattern string
                      std::vector<PatternByte> & pattern //pattern to feed to patternfind
                     );

//returns: offset to data when found, -1 when not found
size_t patternfind(
    const unsigned char* data, //data
    size_t datasize, //size of data
    const std::vector<PatternByte> & pattern //pattern to search
);

#endif // _PATTERNFIND_H
