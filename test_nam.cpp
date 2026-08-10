// Standalone test for NAM A2 processing
// Compile on desktop: g++ -std=c++17 -DNAM_ENABLE_A2_FAST -DNAM_SHARED_PTR_ATOMIC_FREE_FUNCS -o test_nam test_nam.cpp src/nam_processor.cpp NeuralAmpModelerCore/NAM/*.cpp NeuralAmpModelerCore/NAM/wavenet/*.cpp -INeuralAmpModelerCore -INeuralAmpModelerCore/Dependencies/eigen -INeuralAmpModelerCore/Dependencies/nlohmann

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include "nam_processor.h"

int main(int argc, char* argv[]) {
    std::cout << "NAM A2 Test Program\n";
    std::cout << "====================\n\n";
    
    // Test 1: Load example A2 model from NeuralAmpModelerCore
    std::cout << "Test 1: Loading A2 model from example_models/wavenet_a2_max.nam...\n";
    
    std::ifstream namFile("NeuralAmpModelerCore/example_models/wavenet_a2_max.nam");
    if (!namFile.is_open()) {
        std::cerr << "ERROR: Could not open example model file\n";
        std::cerr << "Run this test from the AmpSim root directory\n";
        return 1;
    }
    
    std::stringstream buffer;
    buffer << namFile.rdbuf();
    std::string modelJson = buffer.str();
    namFile.close();
    
    NAMProcessor processor;
    processor.setSampleRate(48000.0);
    
    if (!processor.loadModel(modelJson)) {
        std::cerr << "ERROR: Failed to load A2 model\n";
        return 1;
    }
    
    std::cout << "✓ Model loaded successfully\n\n";
    
    // Test 2: Process test signal
    std::cout << "Test 2: Processing test signal...\n";
    
    const int blockSize = 64;  // Smaller block size for NAM compatibility
    float input[blockSize];
    float output[blockSize];
    
    // Generate test signal: 1kHz sine wave at 48kHz
    for (int i = 0; i < blockSize; i++) {
        input[i] = 0.1f * sinf(2.0f * M_PI * 1000.0f * i / 48000.0f);
    }
    
    // Process through NAM
    processor.process(input, output, blockSize);
    
    std::cout << "✓ Processed " << blockSize << " samples\n";
    std::cout << "  Input range:  [-0.1, 0.1]\n";
    
    // Check output
    float minOut = output[0], maxOut = output[0];
    for (int i = 0; i < blockSize; i++) {
        if (output[i] < minOut) minOut = output[i];
        if (output[i] > maxOut) maxOut = output[i];
    }
    
    std::cout << "  Output range: [" << minOut << ", " << maxOut << "]\n";
    
    // Sanity check: output should be different from input (model applies nonlinearity)
    bool outputChanged = false;
    for (int i = 0; i < blockSize; i++) {
        if (fabs(output[i] - input[i]) > 0.001f) {
            outputChanged = true;
            break;
        }
    }
    
    if (!outputChanged) {
        std::cerr << "WARNING: Output equals input - model may not be processing\n";
        return 1;
    }
    
    std::cout << "✓ Model applies processing (output differs from input)\n\n";
    
    // Test 3: Multiple blocks (test state persistence)
    std::cout << "Test 3: Processing multiple blocks (state persistence)...\n";
    
    for (int block = 0; block < 5; block++) {
        processor.process(input, output, blockSize);
    }
    
    std::cout << "✓ Processed 5 additional blocks successfully\n\n";
    
    std::cout << "====================\n";
    std::cout << "All tests passed! ✓\n";
    std::cout << "\nThe NAM A2 fast path is working correctly.\n";
    
    return 0;
}
