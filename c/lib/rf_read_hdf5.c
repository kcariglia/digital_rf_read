/*
 * Copyright (c) 2017 Massachusetts Institute of Technology (MIT)
 * All rights reserved.
 *
 * Distributed under the terms of the BSD 3-clause license.
 *
 * The full license is in the LICENSE file, distributed with this software.
*/
/* Implementation of rf_hdf5 library
 *
  See digital_rf.h for overview of this module.

  Written 9/2024 by K. Cariglia

  $Id$
*/

/*
note 2 self
------------------
dont forget to remove:
    all printf debug statements
    all fflush(stdout)

dont forget to free rpath, dirlist, overall read obj, etc
*/

#ifdef _WIN32
#  include "wincompat.h"
#  include <Windows.h>
#else
#  include <unistd.h>
#  include <glob.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <fnmatch.h>

#include "digital_rf.h"
#include "hdf5.h"
#include <Python.h>


// helper function(s)
int check_file_exists(const char *directory, const char *pattern) 
/*
pulled from phind: https://www.phind.com/search?cache=o3i451qcuvimrldkp92h97tw
*/
{
    DIR *dir;
    struct dirent *ent;
    int exists = 0;

    //printf("checking dir %s\n", directory);

    // Open the directory
    dir = opendir(directory);
    if (!dir) {
        perror("Failed to open directory");
        return 0;
    }

    // Iterate through directory entries
    while ((ent = readdir(dir)) != NULL) {
        // Check if the entry matches our pattern
        if (fnmatch(pattern, ent->d_name, FNM_NOESCAPE) == 0) {
            closedir(dir);
            return 1; // File found
        }
    }
    // Close directory
    closedir(dir);
    return exists;
}


void get_fraction(uint64_t value, int *numerator, int *denominator) 
/*
another one from phind: https://www.phind.com/search?cache=jvuj6ndkkxrc5kqguk7ezzyd
*/
{
    // Check if the value is less than 1
    if (value < 1) {
        *numerator = 0;
        *denominator = 1;
        return;
    }

    // Check if the value is an integer
    if (value >= (1ULL << 63)) {
        *numerator = (int)value;
        *denominator = 1;
        return;
    }

    // Separate the whole part and fractional bits
    uint64_t whole_part = value & ((1ULL << 63) - 1);
    uint64_t fractional_bits = value >> 63;

    // Handle potential overflow
    if (fractional_bits > 0) {
        *numerator = (int)(whole_part + (fractional_bits / (1ULL << 52)));
        *denominator = 1ULL << 52; // 52 bits for the fraction
    } else {
        *numerator = (int)whole_part;
        *denominator = 1;
    }
}


int compVersions ( const char * version1, const char * version2 ) 
/*
helper function pulled from stack overflow
https://stackoverflow.com/questions/15057010/comparing-version-numbers-in-c
*/
{
    unsigned major1 = 0, minor1 = 0, bugfix1 = 0;
    unsigned major2 = 0, minor2 = 0, bugfix2 = 0;
    sscanf(version1, "%u.%u.%u", &major1, &minor1, &bugfix1);
    sscanf(version2, "%u.%u.%u", &major2, &minor2, &bugfix2);
    if (major1 < major2) return -1;
    if (major1 > major2) return 1;
    if (minor1 < minor2) return -1;
    if (minor1 > minor2) return 1;
    if (bugfix1 < bugfix2) return -1;
    if (bugfix1 > bugfix2) return 1;
    return 0;
}


void _read_properties(top_level_dir_properties * dir_props, char* chan_path) 
/*
docs here
*/
{
  if (strcmp(dir_props->access_mode, "local") == 0)
  {
    char * prop_match = "drf_properties.h5";
    //char * prop_d_match = "dmd_properties.h5"; // when to account for this? not in example
    char * old_prop_match = "metadata.h5";
    // is there a better way to get the regex patterns from list_drf?
    // TODO
    int prop_exists = check_file_exists(chan_path, prop_match);
    int old_prop_exists = check_file_exists(chan_path, old_prop_match);

    char prop_filename[SMALL_HDF5_STR];
    strcpy(prop_filename, chan_path);
    strcat(prop_filename, "/");

    if (prop_exists) {
      strcat(prop_filename, prop_match);
    }
    else if (old_prop_exists) {
      strcat(prop_filename, old_prop_match);
    }
    else if (!(prop_exists || old_prop_exists)) {
      fprintf(stderr, "Properties file not found\n");
      exit(-7);
    }


    // here prop_filename should exist
    hid_t prop_file, fapl, attr_id, group_id, obj_id;
    char attr_name[SMALL_HDF5_STR];
    hsize_t size;
    herr_t status;
    H5O_info_t info;

    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) == H5I_INVALID_HID) {
      fprintf(stderr, "Problem opening file %s\n", prop_filename);
      exit(-8);
    }

    unsigned mode = H5F_ACC_RDONLY;
    if ((prop_file = H5Fopen(prop_filename, mode, fapl)) == H5I_INVALID_HID) {
      fprintf(stderr, "Problem opening file %s\n", prop_filename);
      exit(-9);
    }

    H5Fget_filesize(prop_file, &size);
    if (size <= 0) {
      fprintf(stderr, "No data found in file %s\n", prop_filename);
      exit(-10);
    }

    // get group info and total number of attributes
    if (H5Oget_info_by_name(prop_file, ".", &info, H5O_INFO_ALL, H5P_DEFAULT) < 0) {
      fprintf(stderr, "Unable to get root group info\n");
      exit(-11);
    }

    hsize_t n = (hsize_t)info.num_attrs;
    H5O_info_t ainfo;
    hid_t lapl_id;
    hid_t attr_dtype;
    uint64_t num, den, sps;
    int old = 0;



    // iterate through all attributes in root group
    for (hsize_t i = 0; i < n; i++) {
      // start by getting attribute id
      if ((attr_id = H5Aopen_idx(prop_file, i)) < 0){//".", H5_INDEX_NAME, H5_ITER_INC, i, attr_name, H5P_DEFAULT, lapl_id)) < 0) {
        fprintf(stderr, "Problem accessing attributes\n");
        exit(-12);
      }
      // get attribute name
      H5Aget_name(attr_id, SMALL_HDF5_STR, attr_name);
      //printf("found attr with name %s\n", attr_name);

      // attr_id = H5Aopen_by_name(prop_file, ".", attr_name, H5P_DEFAULT, lapl_id);
      // if (attr_id < 0) {
      //   fprintf(stderr, "Couldn't open attribute %s\n", attr_name);
      // }

      // get attribute dtype
      attr_dtype = H5Aget_type(attr_id);
      
      // i wish switch blocks worked with strings
      // the following is in lieu of that
      // dont forget to double check that dir_props is 
      // set the way it should be
      if (strcmp(attr_name, "digital_rf_time_description") == 0) {
        char time_desc[MED_HDF5_STR];
        if ((status = H5Aread(attr_id, attr_dtype, &time_desc)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
        //printf("time desc %s\n", time_desc);
        fflush(stdout);
        dir_props->drf_time_desc = malloc((strlen(time_desc) + 1) * sizeof(char));
        strcpy(dir_props->drf_time_desc, time_desc);
      } else if (strcmp(attr_name, "digital_rf_version") == 0) {
        char version[SMALL_HDF5_STR];
        if ((status = H5Aread(attr_id, attr_dtype, &version)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
        //printf("version %s\n", version);
        fflush(stdout);
        if (compVersions(dir_props->min_version, version) == 1) {
          // min version > this version, error
          fprintf(stderr, "The Digital RF files being read are version %s, which is less than the required version (%s)\n", version, dir_props->min_version);
          exit(-14);
        }
        if (compVersions(version, dir_props->max_version) == 1) {
          // this version > max version, error
          fprintf(stderr, "The Digital RF files being read are version %s, which is higher than the maximum supported version (%s) for this digital_rf package. If you encounter errors, you will have to upgrade to at least version %s of digital_rf.\n", version, dir_props->max_version, version);
          exit(-14);
        }
        dir_props->version = malloc((strlen(version) + 1) * sizeof(char));
        strcpy(dir_props->version, version);
        // double check version validity here, FIX ME
      } else if (strcmp(attr_name, "epoch") == 0) {
        char epoch[SMALL_HDF5_STR];
        if ((status = H5Aread(attr_id, attr_dtype, &epoch)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
        //printf("epoch %s\n", epoch);
        fflush(stdout);
        dir_props->epoch = malloc((strlen(epoch) + 1) * sizeof(char));
        strcpy(dir_props->epoch, epoch);
      } else if (strcmp(attr_name, "file_cadence_millisecs") == 0) {
        if ((status = H5Aread(attr_id, attr_dtype, &dir_props->file_cadence_millisecs)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
      } else if (strcmp(attr_name, "is_complex") == 0) {
        if ((status = H5Aread(attr_id, attr_dtype, &dir_props->is_complex)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
      } else if (strcmp(attr_name, "is_continuous") == 0) {
        if ((status = H5Aread(attr_id, attr_dtype, &dir_props->is_continuous)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
      } else if (strcmp(attr_name, "num_subchannels") == 0) {
        if ((status = H5Aread(attr_id, attr_dtype, &dir_props->num_subchannels)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
      } else if (strcmp(attr_name, "sample_rate_numerator") == 0) {
        if ((status = H5Aread(attr_id, attr_dtype, &num)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
      } else if (strcmp(attr_name, "sample_rate_denominator") == 0) {
        if ((status = H5Aread(attr_id, attr_dtype, &den)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
      } else if (strcmp(attr_name, "samples_per_second") == 0) {
        // set flag to denote old properties file
        old = 1;
        if ((status = H5Aread(attr_id, attr_dtype, &sps)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
      } else if (strcmp(attr_name, "subdir_cadence_secs") == 0) {
        if ((status = H5Aread(attr_id, attr_dtype, &dir_props->subdir_cadence_secs)) < 0) {
          fprintf(stderr, "Problem reading attribute %s\n", attr_name);
          exit(-13);
        }
      }

      H5Aclose(attr_id);
    }

    if (old) {
      // old version of properties file
      num = (int)num;
      den = (int)den;
      get_fraction(sps, &num, &den);

      dir_props->sample_rate = (long double)sps;
      dir_props->sample_rate_numerator = (uint64_t)num;
      dir_props->sample_rate_denominator = (uint64_t)den;

    } else {
      // new version of properties file
        dir_props->sample_rate_numerator = num;
        dir_props->sample_rate_denominator = den;
        long double sample_rate;
        sample_rate = (long double)num / (long double)den;
        dir_props->sample_rate = sample_rate;
    }





    // the following works but itd be nice to iterate through all the 
    // attributes instead
     
    /*
    // first, check if older version
    // attribute to get here is "samples_per_second"
    //

    if ((attr_id = H5Aopen(prop_file, "samples_per_second", H5P_DEFAULT)) == H5I_INVALID_HID) {
      // new version
      uint64_t numerator, denominator;
      hid_t attr_dtype;

      // get numerator
      if ((attr_id = H5Aopen(prop_file, "sample_rate_numerator", H5P_DEFAULT)) == H5I_INVALID_HID) {
        fprintf(stderr, "Problem accessing sample_rate_numerator\n");
        exit(-12);
      }
      attr_dtype = H5Aget_type(attr_id);
      if ((status = H5Aread(attr_id, attr_dtype, &numerator)) < 0) {
        fprintf(stderr, "Problem reading %s\n", prop_filename);
        exit(-13);
      }
      dir_props->sample_rate_numerator = numerator;
      if ((status = H5Aclose(attr_id)) < 0) {
        fprintf(stderr, "Problem closing %s\n", prop_filename);
        exit(-14);
      }

      // get denominator
      if ((attr_id = H5Aopen(prop_file, "sample_rate_denominator", H5P_DEFAULT)) == H5I_INVALID_HID) {
        fprintf(stderr, "Problem accessing sample_rate_denominator\n");
        exit(-15);
      }
      attr_dtype = H5Aget_type(attr_id);
      if ((status = H5Aread(attr_id, attr_dtype, &denominator)) < 0) {
        fprintf(stderr, "Problem reading %s\n", prop_filename);
        exit(-16);
      }
      dir_props->sample_rate_denominator = denominator;
      if ((status = H5Aclose(attr_id)) < 0) {
        fprintf(stderr, "Problem closing %s\n", prop_filename);
        exit(-17);
      }

      // get sample rate
      long double sample_rate;
      sample_rate = (long double)numerator / (long double)denominator;
      dir_props->sample_rate = sample_rate;
      
      
    } else {
      // old version
      int numerator, denominator;
      uint64_t samples_per_sec;
      hid_t attr_dtype;
      attr_dtype = H5Aget_type(attr_id);
      if ((status = H5Aread(attr_id, attr_dtype, &samples_per_sec)) < 0) {
        fprintf(stderr, "Problem reading %s\n", prop_filename);
        exit(-12);
      }
      get_fraction(samples_per_sec, &numerator, &denominator);

      dir_props->sample_rate = (long double)samples_per_sec;
      dir_props->sample_rate_numerator = (uint64_t)numerator;
      dir_props->sample_rate_denominator = (uint64_t)denominator;
      if ((status = H5Aclose(attr_id)) < 0) {
        fprintf(stderr, "Problem closing %s\n", prop_filename);
        exit(-13);
      }
    }*/
    
    //H5Gclose(group_id); 
    H5Fclose(prop_file);

    //tmp only
    // printf("sample rate: %Lf\n", dir_props->sample_rate);
    // printf("numerator: %d\n", dir_props->sample_rate_numerator);
    // printf("denominator: %d\n", dir_props->sample_rate_denominator);

  } else {
    fprintf(stderr, "access mode %s not implemented\n", dir_props->access_mode);
    exit(-6);
  }

}


channel_properties* _get_channel_properties(char* top_lev_dir, char* chan_path, char* chan_name,
                    char* access, uint64_t rdcc_nbytes)
/*
docs here
*/
{
  top_level_dir_properties * dir_props = NULL;

  /* allocate overall object */
  if ((dir_props = (top_level_dir_properties *)malloc(sizeof(top_level_dir_properties)))==0)
  {
    fprintf(stderr, "malloc failure - unrecoverable\n");
    exit(-6);
  }

  dir_props->access_mode = access;
  dir_props->top_level_dir = top_lev_dir;
  dir_props->channel_name = chan_name;
  dir_props->rdcc_nbytes = rdcc_nbytes;
  dir_props->cachedFilename = NULL;
  dir_props->min_version = "2.0"; // hardcoded just like the python version
  dir_props->max_version = DIGITAL_RF_VERSION;
  //dir_props->cachedFile = NULL;
  _read_properties(dir_props, chan_path);




  channel_properties * channel = NULL;
  if ((channel = (channel_properties *)malloc(sizeof(channel_properties)))==0)
    {
      fprintf(stderr, "malloc failure - unrecoverable\n");
      exit(-6);
    }

  // set some channel attributes here
  channel->channel_name = chan_name;
  channel->top_level_dir_meta = dir_props;


  return(channel);

}


void _get_channels_in_dir(Digital_rf_read_object * drf_read_obj)
/*
docs here, FIX ME

need to account for channels AND SUBCHANNELS!!!!!
*/
{
  char * top_level_dir = drf_read_obj->top_level_directory;
  // first, make sure there is no prop file in top level dir
  char * prop_match = "drf_properties.h5";
  // dmd is for metadata only, not sure where to look for this yet
  //char * prop_d_match = "dmd_properties.h5";
  char * old_prop_match = "metadata.h5";
  // is there a better way to get the regex patterns from list_drf?
  // TODO
  int prop_exists = check_file_exists(top_level_dir, prop_match);
  int old_prop_exists = check_file_exists(top_level_dir, old_prop_match);
  if (prop_exists || old_prop_exists) {
    fprintf(stderr, "%s is a channel directory, but a top-level directory containing channel directories is required.\n", top_level_dir);
    exit(-3);
  }

  DIR * dir;
  struct dirent * ent;
  fflush(stdout);

  dir = opendir(top_level_dir);
  if (dir == NULL) {
    fprintf(stderr, "Problem opening directory %s\n", top_level_dir);
    exit(-4);
  }

  // note: youre gonna have a bad time with these pointers
  // dirlist should be the final string array of all 
  // channel dirs and subdirs
  char ** dirlist = NULL;
  int num_channels = 0;
  int capacity = SMALL_HDF5_STR;
  channel_properties ** channels = NULL;
  channel_properties * channel = NULL;

  //printf("channels ptr les go %p\n", channels);


  // loop through dirs (non recursive)
  // get the recursive part working later ig...

  // char * rpath;
  // rpath = malloc(SMALL_HDF5_STR * sizeof(char));
  // if (rpath == NULL) {
  //   fprintf(stderr, "Malloc failure\n");
  //   exit(-6);
  // }
  // ^^ this might be a problem actually
  char rpath[MED_HDF5_STR];

  char current_dir[SMALL_HDF5_STR];

  while ((ent = readdir(dir)) != NULL) {
    if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0)) {
      continue;
    }
    //printf("found dir %s\n", ent->d_name);
    fflush(stdout);

    current_dir[0] = '\0';
    strcat(current_dir, top_level_dir);
    strcat(current_dir, "/");
    strcat(current_dir, ent->d_name);

    char* res = realpath(current_dir, rpath);

    if (res) {
      //printf("real path is %s\n", rpath);
      //printf("current dir is %s\n", current_dir);

      prop_exists = check_file_exists(current_dir, prop_match);
      old_prop_exists = check_file_exists(current_dir, old_prop_match);

      if (prop_exists || old_prop_exists) {
        // rpath and current dir should be the same, but
        // just in case, use rpath 

        dirlist = realloc(dirlist, (1 + num_channels) * sizeof(char*));  
        if (!dirlist) {
          fprintf(stderr, "Realloc failure\n");
          exit(-5);
        }

        dirlist[num_channels] = malloc((strlen(rpath) + 1) * sizeof(char));
        strcpy(dirlist[num_channels], ent->d_name);
        //printf("found channel: %s\n", dirlist[num_channels]);
        fflush(stdout);

        if (num_channels == 0){
          // start allocating memory for channels arr
          // this is no different than just having the realloc like i did before..
          channels = (channel_properties**)malloc(sizeof(channel_properties*));
          if (!channels) {
            fprintf(stderr, "Malloc failure\n");
            exit(-5);
          }
        } else {
          channels = (channel_properties**)realloc(channels, (1 + num_channels) * sizeof(channel_properties*));
          if (!channels) {
            fprintf(stderr, "Realloc failure\n");
            exit(-5);
          }
        }

        //channels[num_channels] = malloc(sizeof(channel_properties*));

        //channel_properties * channel;// = NULL;
        channel = _get_channel_properties(top_level_dir, rpath, ent->d_name, drf_read_obj->access_mode, drf_read_obj->rdcc_nbytes);
        channels[num_channels] = channel;
        
        num_channels++;
        

      }
    }
    
  }
  drf_read_obj->num_channels = num_channels;
  drf_read_obj->channel_names = dirlist;
  drf_read_obj->channels = channels;

}



Digital_rf_read_object * digital_rf_create_read_hdf5(char * directory, uint64_t rdcc_nbytes)
/* 
documentation goes here 
just init the read object from top level dir
*/
{
  // double check that directory is valid
  // is the string "/" a valid input directory? its local and an abspath..
  if (strlen(directory) < 1) {
    fprintf(stderr, "Malformed input directory\n");
    exit(-1);
  }

  char start_dir[8];
  memcpy(start_dir, &directory[0], 7);

  char * abspath = NULL;

  char * access_mode = NULL;

  // first determine the type of the top level dir
  if (strstr(start_dir, "file://") != NULL) {
    access_mode = "file";
    abspath = directory;
  } else if (strstr(start_dir, "http://") != NULL) {
    access_mode = "http";
    abspath = directory;
  } else if (strstr(start_dir, "ftp://")) {
    access_mode = "ftp";
    abspath = directory;
  } else {
    // local dir
    access_mode = "local";
    abspath = realpath(directory, NULL);
  }

  Digital_rf_read_object * read_obj = NULL;

  /* allocate overall object */
	if ((read_obj = (Digital_rf_read_object *)malloc(sizeof(Digital_rf_read_object)))==0)
	{
		fprintf(stderr, "malloc failure - unrecoverable\n");
		exit(-1);
	}

  // now get channels in top level dir
  // a channel is any subdir with drf_properties.h5

  // only works locally
  if (strcmp(access_mode, "local") != 0) {
    fprintf(stderr, "access mode %s not implemented\n", access_mode);
    exit(-2);
  }

  read_obj->top_level_directory = abspath;
  read_obj->access_mode = access_mode;
  read_obj->rdcc_nbytes = rdcc_nbytes;
  _get_channels_in_dir(read_obj); // works locally only




  return(read_obj);

}


char ** get_channels(Digital_rf_read_object * drf_read_obj)
/*
also docs here
*/
{
  // you still have to sort this alphabetically
  return(drf_read_obj->channel_names);

}

void digital_rf_close_read_hdf5(Digital_rf_read_object * drf_read_obj) 
/*
docs here, FIX ME
*/
{
  if (drf_read_obj != NULL) {
    if (drf_read_obj->top_level_directory != NULL) {
      //printf("finna free top lev dir\n");
      free(drf_read_obj->top_level_directory);
      //printf("freed top lev dir\n");
    }

    // you never malloc'd the access_mode string dummy
    // if (drf_read_obj->access_mode != NULL) {
    //   free(drf_read_obj->access_mode);
    //   printf("freed access mode\n");
    // }

    if (drf_read_obj->num_channels > 0) {
      for (int i = 0; i < drf_read_obj->num_channels; i++) {
        free(drf_read_obj->channel_names[i]);
        //printf("freed chan name\n");
        
        free(drf_read_obj->channels[i]->top_level_dir_meta->version);
        //printf("freed version\n");
        fflush(stdout);
        free(drf_read_obj->channels[i]->top_level_dir_meta->epoch);
        //printf("freed epoch\n");
        fflush(stdout);
        free(drf_read_obj->channels[i]->top_level_dir_meta->drf_time_desc);
        //printf("freed time desc\n");
        free(drf_read_obj->channels[i]->top_level_dir_meta);
        //printf("freed top lev prop obj\n");
        free(drf_read_obj->channels[i]);
        //printf("freed chan obj\n");
      }
    }

    drf_read_obj->num_channels = 0;
    free(drf_read_obj);
    //printf("freed read obj\n");
  }

}


int * get_bounds(Digital_rf_read_object * drf_read_obj, char * channel_name)
/*
more docs here
return a pair of ints
*/
{
  int bounds[2]; // may need long int
  char channel_dir[MED_HDF5_STR];
  int chan_idx = -1;
  int first_unix_sample, last_unix_sample; // may need long int

  if (strcmp(drf_read_obj->access_mode, "local") != 0) {
    fprintf(stderr, "Access mode %s not implemented\n", drf_read_obj->access_mode);
    exit(-15);
  }

  for (int i = 0; i < drf_read_obj->num_channels; i++) {
    // this could definitely be optimized but lets get it working first!
    if (strcmp(drf_read_obj->channel_names[i], channel_name) == 0) {
      chan_idx = i;
    }
  }

  if (chan_idx < 0) {
    fprintf(stderr, "No channel found named %s\n", channel_name);
    exit(-16);
  }

  channel_dir[0] = '\0';
  strcat(channel_dir, drf_read_obj->channels[chan_idx]->top_level_dir_meta->top_level_dir);
  strcat(channel_dir, "/");
  strcat(channel_dir, channel_name);
  // tmp only
  printf("channel dir is  %s\n", channel_dir);
  fflush(stdout);

  //char list_drf[SMALL_HDF5_STR] = "digital_rf";
  
  // init interpreter
  Py_Initialize();
  // import list_drf.py
  //PyObject * modstr = PyUnicode_FromString(list_drf);
  // PyRun_SimpleString("import sys");
  // PyRun_SimpleString("print(sys.path)");
  // printf("made modstr\n");
  // fflush(stdout);

  PyObject * list_drf_mod = PyImport_ImportModule("digital_rf"); // DECREF ME
  if (!list_drf_mod) {
    PyErr_Print();
    fprintf(stderr, "Unable to import digital_rf\n");
    exit(-15);
  } else {
    // looks like best practice is to keep this in this else block
    printf("imported mod\n");
    fflush(stdout);
    // get a reference to list_drf.ilsdrf
    char func[SMALL_HDF5_STR] = "ilsdrf";
    PyObject * ilsdrf = PyObject_GetAttrString(list_drf_mod, func); // DECREF ME
    if (!ilsdrf) {
      printf("yuh\n");
      fflush(stdout);
    }
    printf("got first ref\n");
    fflush(stdout);
    PyObject * args = PyTuple_Pack(9, PyUnicode_FromString(channel_dir), // DECREF ME
                                      Py_False,   // recursive
                                      Py_False,   // reverse
                                      Py_None,    // starttime
                                      Py_None,    // endtime
                                      Py_True,    // include_drf
                                      Py_False,   // include_dmd
                                      Py_None,    // include_drf_properties
                                      Py_None);   // include_dmd_properties
    printf("packed args\n");
    fflush(stdout);
    // py_path_gen is a python generator of drf files and metadata files
    // in the given channel directory, time ordered
    PyObject * py_path_gen = PyObject_CallObject(ilsdrf, args); // DECREF ME
    
    printf("got some kind of result\n");
    fflush(stdout);

    int check = PyIter_Check(py_path_gen);
    printf("iterator? %d\n", check);

    PyObject * path;
    //path = PyIter_Next(py_path_gen); this doesnt work, path is still null
    while ((path = PyIter_Next(py_path_gen))) { // DECREF ME
      printf("its a string right??? %d\n", PyUnicode_Check(path));
      char * datapath = PyUnicode_AsUTF8(path);
      printf("got path %s\n", datapath);

      // ignore properties file
      if (strstr(datapath, "drf_properties.h5") != NULL) {
        Py_DECREF(path);
        continue;
      }
      
      hid_t prop_file, fapl, dset;
      char attr_name[SMALL_HDF5_STR];
      hsize_t size;
      herr_t status;
      H5O_info_t info;

      if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) == H5I_INVALID_HID) {
        fprintf(stderr, "Problem opening file %s\n", datapath);
        exit(-8);
      }

      unsigned mode = H5F_ACC_RDONLY;
      if ((prop_file = H5Fopen(datapath, mode, fapl)) == H5I_INVALID_HID) {
        fprintf(stderr, "Problem opening file %s\n", datapath);
        exit(-9);
      }

      H5Fget_filesize(prop_file, &size);
      if (size <= 0) {
        fprintf(stderr, "No data found in file %s\n", datapath);
        exit(-10);
      }

      if ((dset = H5Dopen2(prop_file, "./rf_data_index", H5P_DEFAULT)) == H5I_INVALID_HID) {
        fprintf(stderr, "Unable to get rf_data_index\n");
        exit(-10);
      }

      // do stuff with dset
      // int(f["rf_data_index"][0][0])  <- this is what u want






      Py_DECREF(path);
      H5Dclose(dset);
      H5Fclose(prop_file);
    }

  


    Py_DECREF(args);
    Py_DECREF(py_path_gen);
    Py_DECREF(ilsdrf);
    Py_DECREF(list_drf_mod);
  }
  
  



  Py_Finalize();

  return(bounds);

}


// // return type for this next one should be an array of data, float probably
// float * read_vector(Digital_rf_read_object * drf_read_obj, int start_sample, int num_samples, char * channel_name)
// /*
// also also docs here
// */
// {

// }

