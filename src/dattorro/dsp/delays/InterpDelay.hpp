#pragma once
#include <vector>
#include <cstdint>
#include <cstring>

class InterpDelay {
public:
    float input = 0.;
    float output = 0.;
    
    InterpDelay(unsigned int maxLength = 512, float initDelayTime = 0.) {
        buffer.resize(maxLength, 0.0f);
        l = maxLength;
        lfloat = static_cast<float>(maxLength);
        setDelayTime(initDelayTime);
    }
    
    #pragma GCC push_options
    #pragma GCC optimize ("Ofast")
    
    inline void process() {
        buffer[w] = input;
        r = w - t;
        
        if (r < 0) {
            r += l;
        }
        
        ++w;
        if (w >= l) {
            w = 0;
        }
        
        upperR = r - 1;
        if (upperR < 0) {
            upperR += l;
        }
        
        dataR = buffer[r];
        dataUpperR = buffer[upperR];
        
        output = dataR + f * (dataUpperR - dataR);
    }
    
    #pragma GCC pop_options
    
    #pragma GCC push_options
    #pragma GCC optimize ("Ofast")
    
    inline float tap(const int &i) {
        j = w - i;
        if (j < 0) {
            j += l;
        }
        return buffer[j];
    }
    
    #pragma GCC pop_options
    
    #pragma GCC push_options
    #pragma GCC optimize ("Ofast")
    
    inline void setDelayTime(float newDelayTime) {
        if (newDelayTime >= lfloat) {
            newDelayTime = lfloat - 1.;
        }
        if (newDelayTime < 0.) {
            newDelayTime = 0.;
        }
        t = static_cast<int>(newDelayTime);
        f = newDelayTime - static_cast<float>(t);
    }
    
    #pragma GCC pop_options
    
    void clear() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        input = 0.;
        output = 0.;
    }
    
private:
    std::vector<float> buffer;
    int w = 0;
    int r = 0;
    int upperR = 0;
    int j = 0;
    int t = 0;
    float f = 0.;
    int l = 512;
    float lfloat = 512.;
    float dataR = 0.;
    float dataUpperR = 0.;
};
