#include "digital_rf.h"

int main(int argc, char* argv[])
{
    Digital_rf_read_object * read_obj = NULL;

    char * dir1 = "/Users/cariglia/example_digital_rf/junk0";

    printf("about to init read obj\n");

    uint64_t b = 4000;
    read_obj = digital_rf_create_read_hdf5(dir1, b);
    printf("init success\n");

    char ** channels = NULL;
    printf("about to get channels\n");
    channels = get_channels(read_obj);

    printf("got channels:\n");
    int num_channels = read_obj->num_subchannels;

    for (int i = 0; i < num_channels; i++) {
        printf("%s\n", channels[i]);
    }

    printf("passed tests if we get here\n");
    return(0);
}