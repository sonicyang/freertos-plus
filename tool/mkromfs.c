#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>

#define hash_init 5381

struct romfs_file_t{
    uint32_t hash;
    uint32_t filename_length;
    uint8_t attribute;
    uint32_t length;
    uint32_t data_offset;
}__attribute__((packed));

uint32_t hash_djb2(const uint8_t * str, uint32_t hash) {
    int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) ^ c;

    return hash;
}

void usage(const char * binname) {
    printf("Usage: %s [-d <dir>] [outfile]\n", binname);
    exit(-1);
}

void reverse_fwrite(FILE* outfile, uint32_t data){
    uint8_t b;
    b = (data >>  0) & 0xff; fwrite(&b, 1, 1, outfile);
    b = (data >>  8) & 0xff; fwrite(&b, 1, 1, outfile);
    b = (data >> 16) & 0xff; fwrite(&b, 1, 1, outfile);
    b = (data >> 24) & 0xff; fwrite(&b, 1, 1, outfile);
    return;
}

void processdir(DIR * dirp, const char * curpath, FILE * outfile, const char * prefix) {
    char fullpath[1024];
    char buf[16 * 1024];
    struct dirent * ent;
    DIR * rec_dirp;
    uint32_t cur_hash = hash_djb2((const uint8_t *) curpath, hash_init);
    uint32_t size, w, hash;
    uint8_t b;
    FILE * infile;
    
    uint32_t filename_length;    
    uint32_t file_count = 0;
    uint32_t current_data_offset = 0;
    
    //Create Dir node
    while ((ent=readdir(dirp))){
	if (strcmp(ent->d_name, ".") == 0)
	    continue;
	if (strcmp(ent->d_name, "..") == 0)
	    continue;
        file_count++;
    }

    hash = hash_djb2((const uint8_t *) curpath, cur_hash);
    filename_length = strlen(ent->d_name);
    
    reverse_fwrite(outfile, hash);
    reverse_fwrite(outfile, filename_length);
    b = 1; fwrite(&b, 1, 1, outfile);

    fseek(infile, 0, SEEK_END);
    size = ftell(infile);
    fseek(infile, 0, SEEK_SET);
    reverse_fwrite(outfile, size);
    reverse_fwrite(outfile, current_data_offset);
    current_data_offset += filename_length + 4 + file_count * 4;

    fwrite(ent->d_name, 1, filename_length, outfile);
    reverse_fwrite(outfile, file_count);
    
    printf("Making romfs.....\n");
    printf("Total Count : %d\n", file_count);

    seekdir(dirp, 0);
    while ((ent = readdir(dirp))) {
        strcpy(fullpath, prefix);
        strcat(fullpath, "/");
        strcat(fullpath, curpath);
        strcat(fullpath, ent->d_name);

        if (strcmp(ent->d_name, ".") == 0)
	    continue;
        if (strcmp(ent->d_name, "..") == 0)
            continue;

        hash = hash_djb2((const uint8_t *) ent->d_name, cur_hash);
	reverse_fwrite(outfile, hash);
	    reverse_fwrite(outfile, filename_length);
  	    b = 0; fwrite(&b, 1, 1, outfile);

            fseek(infile, 0, SEEK_END);
            size = ftell(infile);
            fseek(infile, 0, SEEK_SET);
            reverse_fwrite(outfile, size);
            reverse_fwrite(outfile, current_data_offset);
            current_data_offset += size + filename_length;
            
            fclose(infile);
        }
    }

    seekdir(dirp, 0);
    while ((ent = readdir(dirp))) {
        strcpy(fullpath, prefix);
        strcat(fullpath, "/");
        strcat(fullpath, curpath);
        strcat(fullpath, ent->d_name);
    #ifdef _WIN32
        if (GetFileAttributes(fullpath) & FILE_ATTRIBUTE_DIRECTORY) {
    #else
        if (ent->d_type == DT_DIR) {
    #endif
            if (strcmp(ent->d_name, ".") == 0)
                continue;
            if (strcmp(ent->d_name, "..") == 0)
                continue;
            strcat(fullpath, "/");
            rec_dirp = opendir(fullpath);
            processdir(rec_dirp, fullpath + strlen(prefix) + 1, outfile, prefix);
            closedir(rec_dirp);
        } else {
            infile = fopen(fullpath, "rb");
            if (!infile) {
                perror("opening input file");
                exit(-1);
            }
	    
            fseek(infile, 0, SEEK_END);
            size = ftell(infile);
            fseek(infile, 0, SEEK_SET);

            fwrite(ent->d_name, 1, strlen(ent->d_name), outfile);
            while (size) {
                w = size > 16 * 1024 ? 16 * 1024 : size;
                fread(buf, 1, w, infile);
                fwrite(buf, 1, w, outfile);
                size -= w;
            }
            fclose(infile);
        }
    }
    seekdir(dirp, 0);
    while ((ent = readdir(dirp))) {
        strcpy(fullpath, prefix);
        strcat(fullpath, "/");
        strcat(fullpath, curpath);
        strcat(fullpath, ent->d_name);

        if (strcmp(ent->d_name, ".") == 0)
	    continue;
        if (strcmp(ent->d_name, "..") == 0)
            continue;
    #ifdef _WIN32
        if (GetFileAttributes(fullpath) & FILE_ATTRIBUTE_DIRECTORY) {
    #else
        if (ent->d_type == DT_DIR) {
    #endif
            strcat(fullpath, "/");
            rec_dirp = opendir(fullpath);
            processdir(rec_dirp, fullpath + strlen(prefix) + 1, outfile, prefix);
            closedir(rec_dirp);
        } else {
            hash = hash_djb2((const uint8_t *) ent->d_name, cur_hash);
	    filename_length = strlen(ent->d_name);
            infile = fopen(fullpath, "rb");
            if (!infile) {
                perror("opening input file");
                exit(-1);
            }
	    
            printf("Added : %d %s Offset:%d\n", hash, ent->d_name, current_data_offset);
	    reverse_fwrite(outfile, hash);
	    reverse_fwrite(outfile, filename_length);
  	    b = 0; fwrite(&b, 1, 1, outfile);

            fseek(infile, 0, SEEK_END);
            size = ftell(infile);
            fseek(infile, 0, SEEK_SET);
            reverse_fwrite(outfile, size);
            reverse_fwrite(outfile, current_data_offset);
            current_data_offset += size + filename_length;
            
            fclose(infile);
        }
    }
}

int main(int argc, char ** argv) {
    char * binname = *argv++;
    char * o;
    char * outname = NULL;
    char * dirname = ".";
    uint64_t z = 0;
    FILE * outfile;
    DIR * dirp;

    while ((o = *argv++)) {
        if (*o == '-') {
            o++;
            switch (*o) {
            case 'd':
                dirname = *argv++;
                break;
            default:
                usage(binname);
                break;
            }
        } else {
            if (outname)
                usage(binname);
            outname = o;
        }
    }

    if (!outname)
        outfile = stdout;
    else
        outfile = fopen(outname, "wb");

    if (!outfile) {
        perror("opening output file");
        exit(-1);
    }

    dirp = opendir(dirname);
    if (!dirp) {
        perror("opening directory");
        exit(-1);
    }

    processdir(dirp, "", outfile, dirname);
    fwrite(&z, 1, 8, outfile);
    if (outname)
        fclose(outfile);
    closedir(dirp);
    
    return 0;
}
