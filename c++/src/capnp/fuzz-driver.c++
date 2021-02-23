#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);

static int runOneFile(const char *fname)
{
    // opens the file, get its size, and reads it into a buffer
    uint8_t *data;
    size_t size;
    FILE *fp = fopen(fname, "rb");
    if (fp == NULL) {
        return 2;
    }
    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        return 2;
    }
    size = ftell(fp);
    if (size == (size_t) -1) {
        fclose(fp);
        return 2;
    }
    if (fseek(fp, 0L, SEEK_SET) != 0) {
        fclose(fp);
        return 2;
    }
    data = (uint8_t*) malloc(size);
    if (data == NULL) {
        fclose(fp);
        return 2;
    }
    if (fread(data, size, 1, fp) != 1) {
        fclose(fp);
        free(data);
        return 2;
    }

    //lauch fuzzer
    LLVMFuzzerTestOneInput(data, size);
    free(data);
    fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 1;
    }

    return runOneFile(argv[1]);
}
