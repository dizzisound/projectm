//
// C++ Implementation: PresetLoader
//
// Description:
//
//
// Author: Carmelo Piccione <carmelo.piccione@gmail.com>, (C) 2007
//
// Copyright: See COPYING file that comes with this distribution
//
//
#include "PresetLoader.hpp"
#include "Preset.hpp"
#include "PresetFactory.hpp"
#include <iostream>
#include <sstream>
#include <set>


// win32-dirent definitions //
#include <io.h> /* _findfirst and _findnext set errno iff they return -1 */
#include <stdlib.h>
#include <string.h>
//


#ifdef __unix__
extern "C"
{
#include <errno.h>
}
#endif

#ifdef __APPLE__
extern "C"
{
#include <errno.h>
}
#endif

#include <cassert>
#include "fatal.h"

#include "Common.hpp"






//
#if defined(WIN32) && defined(__cplusplus)
extern "C"
{
#endif

struct dirent
{
    char *d_name;
};

struct DIR
{
    long                handle; /* -1 for failed rewind */
    struct _finddata_t  info;
    struct dirent       result; /* d_name null iff first time */
    char                *name;  /* null-terminated char string */
};

DIR *opendir(const char *name)
{
    DIR *dir = 0;

    if(name && name[0])
    {
        size_t base_length = strlen(name);
        const char *all = /* search pattern must end with suitable wildcard */
            strchr("/\\", name[base_length - 1]) ? "*" : "/*";

        if((dir = (DIR *) malloc(sizeof *dir)) != 0 &&
           (dir->name = (char *) malloc(base_length + strlen(all) + 1)) != 0)
        {
            strcat(strcpy(dir->name, name), all);

            if((dir->handle = (long) _findfirst(dir->name, &dir->info)) != -1)
            {
                dir->result.d_name = 0;
            }
            else /* rollback */
            {
                free(dir->name);
                free(dir);
                dir = 0;
            }
        }
        else /* rollback */
        {
            free(dir);
            dir   = 0;
            errno = ENOMEM;
        }
    }
    else
    {
        errno = EINVAL;
    }

    return dir;
}

int closedir(DIR *dir)
{
    int result = -1;

    if(dir)
    {
        if(dir->handle != -1)
        {
            result = _findclose(dir->handle);
        }

        free(dir->name);
        free(dir);
    }

    if(result == -1) /* map all errors to EBADF */
    {
        errno = EBADF;
    }

    return result;
}

struct dirent *readdir(DIR *dir)
{
    struct dirent *result = 0;

    if(dir && dir->handle != -1)
    {
        if(!dir->result.d_name || _findnext(dir->handle, &dir->info) != -1)
        {
            result         = &dir->result;
            result->d_name = dir->info.name;
        }
    }
    else
    {
        errno = EBADF;
    }

    return result;
}

void rewinddir(DIR *dir)
{
    if(dir && dir->handle != -1)
    {
        _findclose(dir->handle);
        dir->handle = (long) _findfirst(dir->name, &dir->info);
        dir->result.d_name = 0;
    }
    else
    {
        errno = EBADF;
    }
}

// helper for scandir below
static void scandir_free_dir_entries(struct dirent*** namelist, int entries) {
	int i;
	if (!*namelist) return;
	for (i = 0; i < entries; ++i) {
		free((*namelist)[i]);
	}
	free(*namelist);
	*namelist = 0;
}

// returns the number of directory entries select or -1 if an error occurs
int scandir(
	const char* dir, 
	struct dirent*** namelist, 
	int(*filter)(const struct dirent*),
	int(*compar)(const void*, const void*)
) {
	int entries = 0;
	int max_entries = 512; // assume 2*512 = 1024 entries (used for allocation)
	DIR* d;
	
	*namelist = 0;
	
	// open directory
	d = opendir(dir);
	if (!d) return -1;
	
	// iterate
	while (1) {
		struct dirent* ent = readdir(d);
		if (!ent) break;
		
		// add if no filter or filter returns non-zero
		if (filter && (0 == filter(ent))) continue;
		
		// resize our buffer if there is not enough room
		if (!*namelist || entries >= max_entries) {
			struct dirent** new_entries;
			
			max_entries *= 2;
			new_entries = (struct dirent **)realloc(*namelist, max_entries);
			if (!new_entries) {
				scandir_free_dir_entries(namelist, entries);
				closedir(d);
				errno = ENOMEM;
				return -1;
			}
			
			*namelist = new_entries;
		}

		// allocate new entry
		(*namelist)[entries] = (struct dirent *)malloc(sizeof(struct dirent) + strlen(ent->d_name) + 1);
		if (!(*namelist)[entries]) {
			scandir_free_dir_entries(namelist, entries);
			closedir(d);
			errno = ENOMEM;
			return -1;	
		}
		
		// copy entry info
		*(*namelist)[entries] = *ent;
		
		// and then we tack the string onto the end
		{
			char* dest = (char*)((*namelist)[entries]) + sizeof(struct dirent);
			strcpy(dest, ent->d_name);
			(*namelist)[entries]->d_name = dest;
		}

		++entries;
	}
	
	// sort
	if (*namelist && compar) qsort(*namelist, entries, sizeof((*namelist)[0]), compar);
	
	return entries;
}

int alphasort(const void* lhs, const void* rhs) {
	const struct dirent* lhs_ent = *(struct dirent**)lhs;
	const struct dirent* rhs_ent = *(struct dirent**)rhs;
	return _strcmpi(lhs_ent->d_name, rhs_ent->d_name);
}

#ifdef __cplusplus
}
#endif

//










PresetLoader::PresetLoader (int gx, int gy, std::string dirname = std::string()) :_dirname ( dirname ), _dir ( 0 )
{
	_presetFactoryManager.initialize(gx,gy);
	// Do one scan
	if ( _dirname != std::string() )
		rescan();
	else
		clear();
}

PresetLoader::~PresetLoader()
{
	if ( _dir )
		closedir ( _dir );
}

void PresetLoader::setScanDirectory ( std::string dirname )
{
	_dirname = dirname;
}


void PresetLoader::rescan()
{
	// std::cerr << "Rescanning..." << std::endl;

	// Clear the directory entry collection
	clear();

	// If directory already opened, close it first
	if ( _dir )
	{
		closedir ( _dir );
		_dir = 0;
	}

	// Allocate a new a stream given the current directory name
	if ( ( _dir = opendir ( _dirname.c_str() ) ) == NULL )
	{
		handleDirectoryError();
		return; // no files loaded. _entries is empty
	}

	struct dirent * dir_entry;
	std::set<std::string> alphaSortedFileSet;
	std::set<std::string> alphaSortedPresetNameSet;

	while ( ( dir_entry = readdir ( _dir ) ) != NULL )
	{
        if (dir_entry->d_name[0] == 0)
          continue;

		std::ostringstream out;
		// Convert char * to friendly string
		std::string filename ( dir_entry->d_name );

		// Verify extension is projectm or milkdrop
		if (!_presetFactoryManager.extensionHandled(parseExtension(filename)))
			continue;

		if ( filename.length() > 0 && filename[0] == '.' )
			continue;

		// Create full path name
		out << _dirname << PATH_SEPARATOR << filename;

		// Add to our directory entry collection
		alphaSortedFileSet.insert ( out.str() );
		alphaSortedPresetNameSet.insert ( filename );

		// the directory entry struct is freed elsewhere
	}

	// Push all entries in order from the file set to the file entries member (which is an indexed vector)
	for ( std::set<std::string>::iterator pos = alphaSortedFileSet.begin();
	        pos != alphaSortedFileSet.end();++pos )
		_entries.push_back ( *pos );

	// Push all preset names in similar fashion
	for ( std::set<std::string>::iterator pos = alphaSortedPresetNameSet.begin();
	        pos != alphaSortedPresetNameSet.end();++pos )
		_presetNames.push_back ( *pos );

	// Give all presets equal rating of 3 - why 3? I don't know
	_ratings = std::vector<RatingList>(TOTAL_RATING_TYPES, RatingList( _presetNames.size(), 3 ));
	_ratingsSums = std::vector<int>(TOTAL_RATING_TYPES, 3 * _presetNames.size());


	assert ( _entries.size() == _presetNames.size() );



}


std::unique_ptr<Preset> PresetLoader::loadPreset ( unsigned int index )  const
{

	// Check that index isn't insane
	assert ( index < _entries.size() );
	return _presetFactoryManager.allocate
		( _entries[index], _presetNames[index] );

}


std::unique_ptr<Preset> PresetLoader::loadPreset ( const std::string & url )  const
{
//    std::cout << "Loading preset " << url << std::endl;

	try {
		/// @bug probably should not use url for preset name
		return _presetFactoryManager.allocate
				(url, url);
	} catch (const std::exception & e) {
		throw PresetFactoryException(e.what());
	} catch (...) {
		throw PresetFactoryException("preset factory exception of unknown cause");
	}
    return std::unique_ptr<Preset>();
}

void PresetLoader::handleDirectoryError()
{

#ifdef WIN32
	std::cerr << "[PresetLoader] warning: errno unsupported on win32 platforms. fix me" << std::endl;
#else

	switch ( errno )
	{
		case ENOENT:
			std::cerr << "[PresetLoader] ENOENT error. The path \"" << this->_dirname << "\" probably does not exist. \"man open\" for more info." << std::endl;
			break;
		case ENOMEM:
			std::cerr << "[PresetLoader] out of memory! Are you running Windows?" << std::endl;
			abort();
		case ENOTDIR:
			std::cerr << "[PresetLoader] directory specified is not a preset directory! Trying to continue..." << std::endl;
			break;
		case ENFILE:
			std::cerr << "[PresetLoader] Your system has reached its open file limit. Trying to continue..." << std::endl;
			break;
		case EMFILE:
			std::cerr << "[PresetLoader] too many files in use by projectM! Bailing!" << std::endl;
			break;
		case EACCES:
			std::cerr << "[PresetLoader] permissions issue reading the specified preset directory." << std::endl;
			break;
		default:
			break;
	}
#endif
}

void PresetLoader::setRating(unsigned int index, int rating, const PresetRatingType ratingType)
{
	const unsigned int ratingTypeIndex = static_cast<unsigned int>(ratingType);
	assert (index < _ratings[ratingTypeIndex].size());

	_ratingsSums[ratingTypeIndex] -= _ratings[ratingTypeIndex][index];

	_ratings[ratingTypeIndex][index] = rating;
	_ratingsSums[ratingType] += rating;

}


unsigned int PresetLoader::addPresetURL ( const std::string & url, const std::string & presetName, const std::vector<int> & ratings)
{
	_entries.push_back(url);
	_presetNames.push_back ( presetName );

	assert(ratings.size() == TOTAL_RATING_TYPES);
	assert(ratings.size() == _ratings.size());

    for (unsigned int i = 0; i < _ratings.size(); i++)
		_ratings[i].push_back(ratings[i]);

    for (unsigned int i = 0; i < ratings.size(); i++)
		_ratingsSums[i] += ratings[i];

	return _entries.size()-1;
}

void PresetLoader::removePreset ( unsigned int index )
{

	_entries.erase ( _entries.begin() + index );
	_presetNames.erase ( _presetNames.begin() + index );

    for (unsigned int i = 0; i < _ratingsSums.size(); i++) {
		_ratingsSums[i] -= _ratings[i][index];
		_ratings[i].erase ( _ratings[i].begin() + index );
	}


}

const std::string & PresetLoader::getPresetURL ( unsigned int index ) const
{
	return _entries[index];
}

const std::string & PresetLoader::getPresetName ( unsigned int index ) const
{
	return _presetNames[index];
}

int PresetLoader::getPresetRating ( unsigned int index, const PresetRatingType ratingType ) const
{
	return _ratings[ratingType][index];
}

const std::vector<RatingList> & PresetLoader::getPresetRatings () const
{
	return _ratings;
}

const std::vector<int> & PresetLoader::getPresetRatingsSums() const {
	return _ratingsSums;
}

void PresetLoader::setPresetName(unsigned int index, std::string name) {
	_presetNames[index] = name;
}

void PresetLoader::insertPresetURL ( unsigned int index, const std::string & url, const std::string & presetName, const RatingList & ratings)
{
	_entries.insert ( _entries.begin() + index, url );
	_presetNames.insert ( _presetNames.begin() + index, presetName );



    for (unsigned int i = 0; i < _ratingsSums.size();i++) {
		_ratingsSums[i] += _ratings[i][index];
		_ratings[i].insert ( _ratings[i].begin() + index, ratings[i] );
	}

	assert ( _entries.size() == _presetNames.size() );


}
