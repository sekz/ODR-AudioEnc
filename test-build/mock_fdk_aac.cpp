// Mock FDK-AAC for testing
extern "C" {
int aacEncoder_Open(void** phAacEncoder, int encModules, int maxChannels) { return 0; }
int aacEncoder_Close(void** phAacEncoder) { return 0; }
int aacEncoder_SetParam(void* hAacEncoder, int param, int value) { return 0; }
int aacEncoder_Encode(void* hAacEncoder, void* inBufDesc, void* outBufDesc, void* inargs, void* outargs) { return 0; }
}
