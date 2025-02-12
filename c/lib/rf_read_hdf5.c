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

  Written 9/2024 by K Cariglia

  $Id$
*/

/*
note 2 self
------------------
dont forget to remove:
    all printf debug statements
    all fflush(stdout)

get it working first, make it pretty and add documentation later!

*/

#ifdef _WIN32
#  include "wincompat.h"
#  include <Windows.h>
#else
#  include <unistd.h>
#  include <glob.h>
#  include <regex.h>
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
        //perror("Failed to open directory");
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

static int cmpstringp(const void *p1, const void *p2) 
/* pulled from https://www.man7.org/linux/man-pages/man3/qsort.3.html */
{
  return strcmp(*(const char **) p1, *(const char **) p2);
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
    // yes-- but not necessary for this particular use case
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
    hid_t prop_file, fapl, attr_id;
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

      // get attribute dtype
      attr_dtype = H5Aget_type(attr_id);
      
      // i wish switch blocks worked with strings
      // the following is in lieu of that
      if (strcmp(attr_name, "digital_rf_time_description") == 0) {
        char time_desc[BIG_HDF5_STR];
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
      H5Tclose(attr_dtype);
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

    H5Fclose(prop_file);
    H5Pclose(fapl);

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

  dir_props->access_mode = malloc((strlen(access) + 1) * sizeof(char));
  if (!dir_props->access_mode) {
    fprintf(stderr, "Malloc failure\n");
    exit(-23);
  }
  strcpy(dir_props->access_mode, access);

  dir_props->top_level_dir = malloc((strlen(top_lev_dir) + 1) * sizeof(char));
  if (!dir_props->top_level_dir) {
    fprintf(stderr, "Malloc failure\n");
    exit(-24);
  }
  strcpy(dir_props->top_level_dir, top_lev_dir);

  dir_props->channel_name = malloc((strlen(chan_name) + 1) * sizeof(char));
  if (!dir_props->channel_name) {
    fprintf(stderr, "Malloc failure\n");
    exit(-25);
  }
  strcpy(dir_props->channel_name, chan_name);

  dir_props->rdcc_nbytes = rdcc_nbytes;
  dir_props->cachedFilename = NULL;

  dir_props->min_version = malloc(4 * sizeof(char));
  if (!dir_props->min_version) {
    fprintf(stderr, "Malloc failure\n");
    exit(-26);
  }
  strcpy(dir_props->min_version, "2.0"); // hardcoded just like the python version

  dir_props->max_version = malloc((strlen(DIGITAL_RF_VERSION) + 1) * sizeof(char));
  if (!dir_props->max_version) {
    fprintf(stderr, "Malloc failure\n");
    exit(-27);
  }
  strcpy(dir_props->max_version, DIGITAL_RF_VERSION);
  //dir_props->cachedFile = NULL;

  _read_properties(dir_props, chan_path);

  channel_properties * channel = NULL;
  if ((channel = (channel_properties *)malloc(sizeof(channel_properties)))==0)
    {
      fprintf(stderr, "malloc failure - unrecoverable\n");
      exit(-6);
    }

  // set some channel attributes here
  channel->channel_name = malloc((strlen(chan_name) + 1) * sizeof(char));
  if (!channel->channel_name) {
    fprintf(stderr, "Malloc failure\n");
    exit(-7);
  }
  strcpy(channel->channel_name, chan_name);
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
  // dmd is for metadata only, *probably* not needed
  //char * prop_d_match = "dmd_properties.h5";
  char * old_prop_match = "metadata.h5";
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

  // dirlist should be the final string array of all 
  // channel dirs and subdirs
  char ** dirlist = NULL;
  int num_channels = 0;
  int capacity = SMALL_HDF5_STR;
  channel_properties ** channels = NULL;
  channel_properties * channel = NULL;

  // loop through dirs
  char rpath[MED_HDF5_STR];

  char current_dir[SMALL_HDF5_STR];

  while ((ent = readdir(dir)) != NULL) {
    if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0)) {
      continue;
    }
    //printf("found dir %s\n", ent->d_name);
    fflush(stdout);

    current_dir[0] = '\0';
    strcpy(current_dir, top_level_dir);
    strcat(current_dir, "/");
    strcat(current_dir, ent->d_name);

    char* res = realpath(current_dir, rpath);

    if (res) {
      //printf("real path is %s\n", rpath);
      //printf("current dir is %s\n", current_dir);

      prop_exists = check_file_exists(current_dir, prop_match);
      old_prop_exists = check_file_exists(current_dir, old_prop_match);

      if (prop_exists || old_prop_exists) {
        dirlist = realloc(dirlist, (1 + num_channels) * sizeof(char*));  
        if (!dirlist) {
          fprintf(stderr, "Realloc failure\n");
          exit(-5);
        }

        dirlist[num_channels] = malloc((strlen(ent->d_name) + 1) * sizeof(char));
        strcpy(dirlist[num_channels], ent->d_name);
        //printf("found channel: %s\n", dirlist[num_channels]);
        fflush(stdout);

        if (num_channels == 0){
          // start allocating memory for channels arr
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
        channel = _get_channel_properties(drf_read_obj->top_level_directory, current_dir, dirlist[num_channels], drf_read_obj->access_mode, drf_read_obj->rdcc_nbytes);
        channels[num_channels] = channel;
        
        num_channels++;

      }
    }
    
  }
  closedir(dir);
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
  if (strlen(directory) <= 1) {
    fprintf(stderr, "Malformed input directory\n");
    exit(-1);
  }

  //char start_dir[8];
  //memcpy(start_dir, &directory[0], 7);

  char abspath[MED_HDF5_STR];
  char access_mode[SMALL_HDF5_STR];

  // first determine the type of the top level dir
  if (strstr(directory, "file://") != NULL) {
    strcpy(access_mode, "file");
    strcpy(abspath, directory);
  } else if (strstr(directory, "http://") != NULL) {
    strcpy(access_mode, "http");
    strcpy(abspath, directory);
  } else if (strstr(directory, "ftp://") != NULL) {
    strcpy(access_mode, "ftp");
    strcpy(abspath, directory);
  } else {
    // local dir
    strcpy(access_mode, "local");
    realpath(directory, abspath);
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

  read_obj->top_level_directory = malloc((strlen(abspath) + 1) * sizeof(char));
  if (!read_obj->top_level_directory) {
    fprintf(stderr, "Malloc failure\n");
    exit(-12);
  }
  strcpy(read_obj->top_level_directory, abspath);


  read_obj->access_mode = malloc((strlen(access_mode) + 1) * sizeof(char));
  if (!read_obj->access_mode) {
    fprintf(stderr, "Malloc failure\n");
    exit(-3);
  }
  strcpy(read_obj->access_mode, access_mode);
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
  // just do the qsort thing again
  return(drf_read_obj->channel_names);

}

void digital_rf_close_read_hdf5(Digital_rf_read_object * drf_read_obj) 
/*
docs here, FIX ME
*/
{
  if (drf_read_obj != NULL) {
    if (drf_read_obj->top_level_directory != NULL) {
      free(drf_read_obj->top_level_directory);
    }
    if (drf_read_obj->access_mode != NULL) {
      free(drf_read_obj->access_mode);
    }

    if (drf_read_obj->num_channels > 0) {
      for (int i = 0; i < drf_read_obj->num_channels; i++) {
        
        // free top_level_dir strings
        free(drf_read_obj->channels[i]->top_level_dir_meta->access_mode);
        free(drf_read_obj->channels[i]->top_level_dir_meta->top_level_dir);
        free(drf_read_obj->channels[i]->top_level_dir_meta->channel_name);
        free(drf_read_obj->channels[i]->top_level_dir_meta->min_version);
        free(drf_read_obj->channels[i]->top_level_dir_meta->max_version);

        free(drf_read_obj->channels[i]->top_level_dir_meta->version);
        //printf("freed version\n");
        fflush(stdout);
        free(drf_read_obj->channels[i]->top_level_dir_meta->epoch);
        //printf("freed epoch\n");
        fflush(stdout);
        free(drf_read_obj->channels[i]->top_level_dir_meta->drf_time_desc);

        //printf("freed time desc\n");
        free(drf_read_obj->channel_names[i]);

        free(drf_read_obj->channels[i]->channel_name);
        free(drf_read_obj->channels[i]->top_level_dir_meta);
        //printf("freed top lev prop obj\n");
        free(drf_read_obj->channels[i]);
        //printf("freed chan obj\n");
      }
    }
    free(drf_read_obj->channel_names);
    free(drf_read_obj->channels);

    drf_read_obj->num_channels = 0;
    free(drf_read_obj);
    //printf("freed read obj\n");
  }

}


char ** _ilsdrf(Digital_rf_read_object * drf_read_obj, char * chan_name)
/*
docs here
path is assumed to be a channel path (absolute)
*/
{
  char ** fnames = NULL;
  int chan_idx = -1;
  char channel_dir[MED_HDF5_STR];

  for (int i = 0; i < drf_read_obj->num_channels; i++) {
    // this could definitely be optimized but lets get it working first!
    if (strcmp(drf_read_obj->channel_names[i], chan_name) == 0) {
      chan_idx = i;
    }
  }

  if (chan_idx < 0) {
    fprintf(stderr, "No channel found named %s\n", chan_name);
    exit(-16);
  }

  channel_dir[0] = '\0';
  strcpy(channel_dir, drf_read_obj->channels[chan_idx]->top_level_dir_meta->top_level_dir);
  strcat(channel_dir, "/");
  strcat(channel_dir, chan_name);

  bool drf = (check_file_exists(channel_dir, "drf_properties.h5") || check_file_exists(channel_dir, "drf_metadata.h5"));
  bool dmd = (check_file_exists(channel_dir, "dmd_properties.h5") || check_file_exists(channel_dir, "dmd_metadata.h5"));
  
  regex_t re_subdir;
  if (regcomp(&re_subdir, "[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]-[0-9][0-9]-[0-9][0-9]", 0) != 0) {
    fprintf(stderr, "Problem compiling regex\n");
    exit(-20);
  }

  DIR * dir;
  DIR * subdir;
  struct dirent * ent;
  struct dirent * subent;
  char current_dir[MED_HDF5_STR];
  char current_file[MED_HDF5_STR]; // using med str because this is supposed to contain abspath
  //char ** dirnames;
  //int numdirs = 0;
  int numfiles = 0;

  if (drf || dmd) {
    // there must exist some property file for this 
    // to be a valid channel directory

    dir = opendir(channel_dir);
    if (!dir) {
      fprintf(stderr, "Problem opening directory %s\n", channel_dir);
      exit(-21);
    }

    while ((ent = readdir(dir)) != NULL) {
      if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0)) {
        continue;
      }

      if (regexec(&re_subdir, ent->d_name, 0, NULL, 0) == 0) {
        // found matching subdir
        //printf("matched dir %s\n", ent->d_name);
        current_dir[0] = '\0';
        strcpy(current_dir, channel_dir);
        strcat(current_dir, "/");
        strcat(current_dir, ent->d_name);


        // now we have to find the filenames IN ORDER
        // well,,, first just get fnames, THEN sort
        subdir = opendir(current_dir);
        if (!subdir) {
          fprintf(stderr, "Unable to open directory %s\n", current_dir);
          exit(-23);
        }

        while ((subent = readdir(subdir)) != NULL) {
          // get filenames from this subdir
          if ((strstr(subent->d_name, "rf@") != NULL) && (strstr(subent->d_name, ".h5") != NULL)) {
            // valid filename
            if (numfiles == 0) {
              fnames = (char**)malloc((1 + numfiles) * sizeof(char*));
            } else {
              fnames = realloc(fnames, (1 + numfiles) * sizeof(char*));
            }
            
            if (!fnames) {
              fprintf(stderr, "Realloc failure\n");
              exit(-22);
            }

            //printf("found file %s\n", subent->d_name);

            current_file[0] = '\0';
            strcpy(current_file, current_dir);
            strcat(current_file, "/");
            strcat(current_file, subent->d_name);

            fnames[numfiles] = malloc((strlen(current_file) + 1) * sizeof(char));
            strcpy(fnames[numfiles], current_file);
            numfiles++;
          }
        }
        closedir(subdir);
      }
    }
    qsort(fnames, numfiles, sizeof(char*), cmpstringp);
    regfree(&re_subdir);
    closedir(dir);
  }

  // lets try using a sentinel value
  fnames = realloc(fnames, (1 + numfiles) * sizeof(char*));
  if (!fnames) {
    fprintf(stderr, "Realloc failure\n");
    exit(-22);
  }

  char sentinel[] = "sentinel";
  fnames[numfiles] = malloc((strlen(sentinel) + 1) * sizeof(char));
  strcpy(fnames[numfiles], sentinel);

  //printf("total files found: %d\n", numfiles);
  return(fnames);
}


void get_bounds(Digital_rf_read_object * drf_read_obj, char * channel_name,
  drf_bounds * bounds)
/*
more docs here
return a pair of ints
*/
{
  //unsigned long long bounds[2];
  //unsigned long long * bounds = NULL;
  unsigned long long s_bound, e_bound;
  unsigned long long tmp_bounds[2];

  if (strcmp(drf_read_obj->access_mode, "local") != 0) {
    fprintf(stderr, "Access mode %s not implemented\n", drf_read_obj->access_mode);
    exit(-15);
  }

  char ** pathlist = NULL;
  pathlist = _ilsdrf(drf_read_obj, channel_name);

  bool firstpath = true;
  unsigned long long total_samples;
  int pthidx = 0;
  while (pathlist[pthidx] != NULL) {

    if (strstr(pathlist[pthidx], "sentinel") != NULL) {
      pthidx++;
      break;
    }

    char datapath[MED_HDF5_STR];
    strcpy(datapath, pathlist[pthidx]);

    // ignore properties file
    if (strstr(datapath, "drf_properties.h5") != NULL) {
      pthidx++;
      continue;
    }
      
    hid_t prop_file, fapl, dset, dshape, space;
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

    hid_t dsettype;
    dsettype = H5Dget_type(dset);

    if (H5Dread(dset, dsettype, H5S_ALL, H5S_ALL, H5P_DEFAULT, tmp_bounds) < 0) {
      fprintf(stderr, "Unable to get rf_data_index\n");
      exit(-10);
    }

    H5Tclose(dsettype);
    H5Dclose(dset);
    if (firstpath) {
      // set start bound if first path in list
      s_bound = tmp_bounds[0];
      firstpath = false;
    } else {
      // keep resetting last index until you break out of the while loop
      // optimize this later
      if ((dshape = H5Dopen2(prop_file, "./rf_data", H5P_DEFAULT)) == H5I_INVALID_HID) {
        fprintf(stderr, "Unable to get rf_data\n");
        exit(-10);
      }
      space = H5Dget_space(dshape);
      int rank = H5Sget_simple_extent_ndims(space);
      if (rank >= 0) {
        hsize_t dims[rank];
        H5Sget_simple_extent_dims(space, dims, NULL);
        total_samples = dims[0];

          /* printf("Dimensions: ");
          for (int i = 0; i < rank; i++) {
            printf("%lld ", dims[i]);
          }
          printf("\n"); */
      } else {
        fprintf(stderr, "Unable to read rf_data shape\n");
        exit(-18);
      }
      H5Sclose(space);
      H5Dclose(dshape);
      
    }
    
    H5Fclose(prop_file);
    H5Pclose(fapl);
    pthidx++;
  }

  for (int i = 0; i < pthidx; i++) {
    free(pathlist[i]);
  }
  free(pathlist);

  // last_start_sample = tmp_bounds[0]
  // last_index = tmp_bounds[1]
  e_bound = (tmp_bounds[0] + (total_samples - (tmp_bounds[1] + 1)));

  
  bounds->b1 = s_bound;
  bounds->b2 = e_bound;

  //printf("sb is %llu at %p\n", s_bound, &s_bound);
  //printf("eb is %llu at %p\n", e_bound, &e_bound);
  printf("bounds are %llu and %llu at %p and %p\n", bounds->b1, bounds->b2, &bounds->b1, &bounds->b2);
  //printf("bounds is at %p\n", bounds);
  fflush(stdout);
  
  return;
}


// void _read(top_level_dir_properties * dir_props, long start_sample, long long end_sample, char ** paths,
//  long long ** data_dict, bool len_only, int sub_chan_idx)
// /*
// docs here
// */
// {
//   if (strcmp(dir_props->access_mode, "local") != 0) {
//     fprintf(stderr, "Access mode %s not implemented\n", dir_props->access_mode);
//     exit(-20);
//   }

//   bool ignore_subchan = (sub_chan_idx > 0) ? true : false;






// }


// char ** _get_file_list(unsigned long long s0, unsigned long long s1, long double sps, unsigned long long scs, unsigned long long fcm)
// /*
// docs here
// */
// {
//   printf("s0: %llu  s1: %llu\n", s0, s1);
//   unsigned long long dif = s1 - s0;
//   if (dif > 1e12) {
//     printf("dif is %llu\n", dif);
//     fprintf(stderr, "Requested read size, %llu samples, is very large\n", dif);
//   }

//   char ** fileList = NULL;

//   unsigned long long start_ts = (s0 / sps);
//   unsigned long long end_ts = (s1 / sps) + 1;
//   unsigned long long start_msts = ((s0 / sps) * 1000);
//   unsigned long long end_msts = ((s1 / sps) * 1000);

//   unsigned long long start_sub_ts = (floor(start_ts / scs) * scs);
//   unsigned long long end_sub_ts = (floor(end_ts / scs) * scs);

//   printf("start is %llu, end is %llu\n", start_sub_ts, end_sub_ts);
//   /* for (uint64_t sub_ts = start_sub_ts; sub_ts < (end_sub_ts + scs); sub_ts += scs) {
//     struct tm * utc_time;
//     sub_ts = (time_t)sub_ts;
//     time_t sub_time = time(&sub_ts);
//     utc_time = gmtime(&sub_time);

//     char subdir_str[SMALL_HDF5_STR];
//     strftime(subdir_str, SMALL_HDF5_STR, "%Y-%m-%dT%H-%M-%S", utc_time);
//     printf("time is &s\n", subdir_str);

//   } */

//   return(fileList);

// }


// // orderedDict return type
// unsigned long long ** get_continuous_blocks(Digital_rf_read_object * drf_read_obj, unsigned long long start_sample, unsigned long long end_sample, char * channel_name)
// /*
// docs here
// */
// {
//   int chan_idx = -1;

//   for (int i = 0; i < drf_read_obj->num_channels; i++) {
//     // this could definitely be optimized but lets get it working first!
//     if (strcmp(drf_read_obj->channel_names[i], channel_name) == 0) {
//       chan_idx = i;
//     }
//   }

//   if (chan_idx < 0) {
//     fprintf(stderr, "No channel found named %s\n", channel_name);
//     exit(-16);
//   }

//   uint64_t subdir_cadence_secs = drf_read_obj->channels[chan_idx]->top_level_dir_meta->subdir_cadence_secs;
//   uint64_t file_cadence_msecs = drf_read_obj->channels[chan_idx]->top_level_dir_meta->file_cadence_millisecs;
//   long double sample_rate = drf_read_obj->channels[chan_idx]->top_level_dir_meta->sample_rate;

//   char ** paths = NULL;
//   printf("start sample is %llu, end sample is %llu\n", start_sample, end_sample);
//   paths = _get_file_list(start_sample, end_sample, sample_rate, (unsigned long long)subdir_cadence_secs, (unsigned long long)file_cadence_msecs);

//   unsigned long long ** cont_blocks = NULL;
//   _read(drf_read_obj->channels[chan_idx]->top_level_dir_meta, start_sample, end_sample, paths, cont_blocks, false, -1);


//   // still need to combine blocks
//   return(cont_blocks);
// }


// float ** read_vector(Digital_rf_read_object * drf_read_obj, long long start_sample,
//  int num_samples, char * channel_name, char * sub_channel)
// /*
// also also docs here
// */
// {
//   if (num_samples < 1) {
//     fprintf(stderr, "Number of samples requested must be greater than 0, not %d\n", num_samples);
//     exit(-19);
//   }
//   float ** vector = NULL;
//   long long end_sample = start_sample + ((long long)num_samples - 1);

  

//   return(vector);
// }

