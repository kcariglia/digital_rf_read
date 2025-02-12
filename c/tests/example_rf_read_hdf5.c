#include "digital_rf.h"

int main(int argc, char* argv[])
{
    Digital_rf_read_object * read_obj = NULL;
    uint64_t b = 4000;
    char ** channels = NULL;
    char ** channels2 = NULL;
    int num_channels;

    char dir1[SMALL_HDF5_STR] = "/Users/cariglia/example_digital_rf";
    char dir2[SMALL_HDF5_STR] = "/Users/cariglia/Desktop/drfexamples/hprec_channels";
    char dir3[SMALL_HDF5_STR] = "/Users/cariglia/Desktop/drfexamples/hprec_subchannels";

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
        //printf("sample rate again: %Lf\n", read_obj->channels[i]->top_level_dir_meta->sample_rate);
    }

    drf_bounds * bounds2;

    bounds2 = (drf_bounds *)malloc(sizeof(drf_bounds));
    if (!bounds2) {
        fprintf(stderr, "Malloc failure\n");
        exit(-25);
    }

    get_bounds(read_obj, channels[0], bounds2);//"da");
    printf("got bounds: %ld  %ld\n", bounds2->b1, bounds2->b2);

    unsigned long long s0, s1;
    s0 = bounds2->b1;
    s1 = bounds2->b2;
    printf("(main)s0: %llu  s1: %llu\n", s0, s1);
    printf("got bounds (again): %llu  %llu\n", bounds2->b1, bounds2->b2);

    //long long ** cont_data_arr2 = NULL;
    //cont_data_arr2 = get_continuous_blocks(read_obj, bounds2[0], bounds2[1], read_obj->channel_names[0]);

    free(bounds2);
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

    drf_bounds * bounds3;

    bounds3 = (drf_bounds *)malloc(sizeof(drf_bounds));
    if (!bounds3) {
        fprintf(stderr, "Malloc failure\n");
        exit(-25);
    }

    get_bounds(read_obj, "adc", bounds3);
    printf("got bounds: %ld  %ld\n", bounds3->b1, bounds3->b2);

    long long ** cont_data_arr3 = NULL;
    //cont_data_arr3 = get_continuous_blocks(read_obj, bounds3[0], bounds3[1], read_obj->channel_names[0]);

    free(bounds3);
    digital_rf_close_read_hdf5(read_obj);
    read_obj = NULL;


    printf("Test 1: basic example\n");
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
    fflush(stdout);
    drf_bounds * bounds1;

    bounds1 = (drf_bounds *)malloc(sizeof(drf_bounds));
    if (!bounds1) {
        fprintf(stderr, "Malloc failure\n");
        exit(-25);
    }

    get_bounds(read_obj, channels[0], bounds1);//"junk0");
    printf("got bounds: %llu  %llu\n", bounds1->b1, bounds1->b2);
    fflush(stdout);

    unsigned long ** cont_data_arr1 = NULL;
   // unsigned long long s0, s1;
    s0 = bounds1->b1;
    s1 = bounds1->b2;
    printf("(main)s0: %llu  s1: %llu\n", s0, s1);
    printf("got bounds (again): %llu  %llu\n", bounds1->b1, bounds1->b2);
    //cont_data_arr1 = get_continuous_blocks(read_obj, s0, s1, read_obj->channel_names[0]);
    fflush(stdout);
    free(bounds1);
    digital_rf_close_read_hdf5(read_obj);
    read_obj = NULL;

    printf("passed tests if we get here\n");
    return(0);
}