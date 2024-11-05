#include "digital_rf.h"

int main(int argc, char* argv[])
{
    Digital_rf_read_object * read_obj = NULL;
    uint64_t b = 4000;
    char ** channels = NULL;
    char ** channels2 = NULL;
    int num_channels;

    char * dir1 = "/Users/cariglia/example_digital_rf";
    char * dir2 = "/Users/cariglia/Desktop/drfexamples/hprec_channels";
    char * dir3 = "/Users/cariglia/Desktop/drfexamples/hprec_subchannels";

    printf("Test 1: basic examples\n");
    printf("----------------------\n");
    //printf("about to init read obj\n");

    
    read_obj = digital_rf_create_read_hdf5(dir1, b);
    printf("init success\n");

    
    //printf("about to get channels\n");
    channels = get_channels(read_obj);
    
    channels2 = read_obj->channel_names;

    printf("got channels:\n");
    num_channels = read_obj->num_channels;

    for (int i = 0; i < num_channels; i++) {
        printf("%s\n", channels[i]);
        printf("%s\n", channels2[i]);
        printf("sample rate: %Lf\n", read_obj->channels[i]->top_level_dir_meta->sample_rate);
        printf("time desc: %s\n", read_obj->channels[i]->top_level_dir_meta->drf_time_desc);
        printf("version: %s\n", read_obj->channels[i]->top_level_dir_meta->version);
        printf("epoch: %s\n", read_obj->channels[i]->top_level_dir_meta->epoch);
    }

    int * bounds1;
    bounds1 = get_bounds(read_obj, "junk0");
    // print bounds here

    digital_rf_close_read_hdf5(read_obj);
    read_obj = NULL;

    printf("Test 2: real data, no subchannels\n");
    printf("----------------------\n");
    read_obj = digital_rf_create_read_hdf5(dir2, b);
    printf("init success\n");

    //char ** channels = NULL;
    //printf("about to get channels\n");
    channels = get_channels(read_obj);
    //char ** channels2 = NULL;
    channels2 = read_obj->channel_names;

    printf("got channels:\n");
    num_channels = read_obj->num_channels;

    for (int i = 0; i < num_channels; i++) {
        printf("%s\n", channels[i]);
        printf("%s\n", channels2[i]);
        printf("sample rate: %Lf\n", read_obj->channels[i]->top_level_dir_meta->sample_rate);
    }

    int * bounds2;
    bounds2 = get_bounds(read_obj, "da");
    // print bounds here

    digital_rf_close_read_hdf5(read_obj);
    read_obj = NULL;


    printf("Test 3: real data, yes subchannels\n");
    printf("----------------------\n");
    read_obj = digital_rf_create_read_hdf5(dir3, b);
    printf("init success\n");

    //char ** channels = NULL;
    //printf("about to get channels\n");
    channels = get_channels(read_obj);
    //char ** channels2 = NULL;
    channels2 = read_obj->channel_names;

    printf("got channels:\n");
    num_channels = read_obj->num_channels;

    for (int i = 0; i < num_channels; i++) {
        printf("%s\n", channels[i]);
        printf("%s\n", channels2[i]);
        printf("sample rate: %Lf\n", read_obj->channels[i]->top_level_dir_meta->sample_rate);
    }

    int * bounds3;
    bounds3 = get_bounds(read_obj, "adc");
    // print bounds here

    digital_rf_close_read_hdf5(read_obj);
    read_obj = NULL;

    printf("passed tests if we get here\n");
    return(0);
}