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

void write_romfs_file(FILE* outfile, struct romfs_file_t* file){
    reverse_fwrite(outfile, file->hash);
    reverse_fwrite(outfile, file->filename_length);
    fwrite(&(file->attribute), 1, 1, outfile);
    reverse_fwrite(outfile, file->length);
    reverse_fwrite(outfile, file->data_offset);
    return;
}

int processdir_table(DIR * dirp, const char * curpath, FILE * outfile, const char * prefix, int* data_offset, char* curr_dirname) {
    char fullpath[1024];
    struct dirent * ent;
    DIR * rec_dirp;
    uint32_t cur_hash = hash_djb2((const uint8_t *) curpath, hash_init);
    FILE * infile;

    struct romfs_file_t file;
    uint32_t file_count = 0;
    
    //Create Dir node
    while ((ent=readdir(dirp))){
        if (strcmp(ent->d_name, ".") == 0)
            continue;
        if (strcmp(ent->d_name, "..") == 0)
            continue;
        file_count++;
    }

    //hash = hash_djb2((const uint8_t *) curr_dirname, cur_hash);
    
    file.hash = cur_hash;
    file.filename_length = strlen(curr_dirname);
    file.attribute = 1; 
    file.length = 4 + file_count * 4;
    file.data_offset = *data_offset;

    write_romfs_file(outfile, &file);
    *data_offset += file.filename_length + 4 + file_count * 4;

    printf("Adding %s | %s, %d, Offset %d: \n", curpath, curr_dirname, cur_hash, file.data_offset);

    seekdir(dirp, 0);
    while ((ent = readdir(dirp))) {
        strcpy(fullpath, prefix);
        strcat(fullpath, "/");
        strcat(fullpath, curpath);
        strcat(fullpath, ent->d_name);

        if (ent->d_type != DT_DIR) {
            file.hash = hash_djb2((const uint8_t *) ent->d_name, cur_hash);
            file.filename_length = strlen(ent->d_name);
            file.attribute = 0; 

            infile = fopen(fullpath, "rb");
            if (!infile) {
                perror("opening input file");
                exit(-1);
            }

            fseek(infile, 0, SEEK_END);
            file.length = ftell(infile);
            fseek(infile, 0, SEEK_SET);

            fclose(infile);

            file.data_offset = *data_offset;
            *data_offset += file.length + file.filename_length;
           
            write_romfs_file(outfile, &file);

            printf("Adding %s, %d, Offset %d: \n", ent->d_name, file.hash, file.data_offset);
        }
    }

    seekdir(dirp, 0);
    while ((ent = readdir(dirp))) {
        strcpy(fullpath, prefix);
        strcat(fullpath, "/");
        strcat(fullpath, curpath);
        strcat(fullpath, ent->d_name);
        if (ent->d_type == DT_DIR) {
            if (strcmp(ent->d_name, ".") == 0)
                continue;
            if (strcmp(ent->d_name, "..") == 0)
                continue;
            strcat(fullpath, "/");
            rec_dirp = opendir(fullpath);
            file_count += processdir_table(rec_dirp, fullpath + strlen(prefix) + 1, outfile, prefix, data_offset, ent->d_name);
            closedir(rec_dirp);
        }
    }
    
    return file_count;
}

int processdir_data(DIR * dirp, const char * curpath, FILE * outfile, const char * prefix, char* curr_dirname) {
    char fullpath[1024];
    char buf[16 * 1024];
    struct dirent * ent;
    DIR * rec_dirp;
    uint32_t cur_hash = hash_djb2((const uint8_t *) curpath, hash_init);
    uint32_t size, w, hash;
    FILE * infile;
    
    uint32_t file_count = 0;
    
    while ((ent=readdir(dirp))){
        if (strcmp(ent->d_name, ".") == 0)
            continue;
        if (strcmp(ent->d_name, "..") == 0)
            continue;
        file_count++;
    }

    printf("Encoding %s, count : %d\n", curpath, file_count);

    fwrite(curr_dirname, 1, strlen(curr_dirname), outfile);
    reverse_fwrite(outfile, file_count);

    seekdir(dirp, 0);
    while ((ent = readdir(dirp))) {
        if (strcmp(ent->d_name, ".") == 0)
            continue;
        if (strcmp(ent->d_name, "..") == 0)
            continue;
        if(ent->d_type == DT_DIR){
            hash = hash_djb2((const uint8_t*)"/", hash_djb2((const uint8_t*) ent->d_name, cur_hash));  //A tailing / due to it's a directory
            reverse_fwrite(outfile, hash);
        }else{
            hash = hash_djb2((const uint8_t *) ent->d_name, cur_hash);
            printf("Linking %s, %d, %d\n",ent->d_name ,cur_hash, hash);
            reverse_fwrite(outfile, hash);
        } 
    }

    seekdir(dirp, 0);
    while ((ent = readdir(dirp))) {
        strcpy(fullpath, prefix);
        strcat(fullpath, "/");
        strcat(fullpath, curpath);
        strcat(fullpath, ent->d_name);
        if (ent->d_type != DT_DIR) {
            fwrite(ent->d_name, 1, strlen(ent->d_name), outfile);

            infile = fopen(fullpath, "rb");
            if (!infile) {
                perror("opening input file");
                exit(-1);
            }

            fseek(infile, 0, SEEK_END);
            size = ftell(infile);
            fseek(infile, 0, SEEK_SET);
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
        if (ent->d_type == DT_DIR) {
            strcat(fullpath, "/");
            rec_dirp = opendir(fullpath);
            processdir_data(rec_dirp, fullpath + strlen(prefix) + 1, outfile, prefix, ent->d_name);
            closedir(rec_dirp);
        }
    }

    return 0;
}
int main(int argc, char ** argv) {
    char * binname = *argv++;
    char buf[16 * 1024];
    char * o;
    char * outname = NULL;
    char out_table_name[2048];
    char out_data_name[2048];
    char * dirname = ".";
    FILE * outfile;
    FILE * outfile_table;
    FILE * outfile_data;
    DIR * dirp;
    uint32_t w, size;

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
    else{
        outfile = fopen(outname, "wb");
        strcpy(out_table_name, outname);
        strcpy(out_data_name, outname);
        strcat(out_table_name, ".table");
        strcat(out_data_name, ".data");
        outfile_table = fopen(out_table_name, "wb");
        outfile_data = fopen(out_data_name, "wb");
    }

    if (!outfile) {
        perror("opening output file");
        exit(-1);
    }

    dirp = opendir(dirname);
    if (!dirp) {
        perror("opening directory");
        exit(-1);
    }

    int data_offset = 0;
    int n = processdir_table(dirp, "", outfile_table, dirname, &data_offset, "");
    
    seekdir(dirp, 0);

    processdir_data(dirp, "", outfile_data, dirname, "");
    if (outname){
        fclose(outfile_table);
        fclose(outfile_data);
    }

    outfile_table = fopen(out_table_name, "rb");
    outfile_data = fopen(out_data_name, "rb");

    reverse_fwrite(outfile, n + 1); //root dir
    
    fseek(outfile_table, 0, SEEK_END);
    size = ftell(outfile_table);
    fseek(outfile_table, 0, SEEK_SET);

    while (size) {
        w = size > 16 * 1024 ? 16 * 1024 : size;
        fread(buf, 1, w, outfile_table);
        fwrite(buf, 1, w, outfile);
        size -= w;
    }

    fseek(outfile_data, 0, SEEK_END);
    size = ftell(outfile_data);
    fseek(outfile_data, 0, SEEK_SET);

    while (size) {
        w = size > 16 * 1024 ? 16 * 1024 : size;
        fread(buf, 1, w, outfile_data);
        fwrite(buf, 1, w, outfile);
        size -= w;
    }

    if (outname){
        fclose(outfile);
        fclose(outfile_table);
        fclose(outfile_data);
    }
    closedir(dirp);
    
    return 0;
}
