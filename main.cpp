#include <stdio.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <math.h>
#include "minilzo.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

int processFile(std::fstream* in_file, std::fstream* out_file, std::fstream* log_file)
{
    char* log_s = new char[0x200];
    u32 log_s_len = 0;

    lzo_uint in_filesize = 0;
    in_file->seekg(0, std::ios::end);
    in_filesize = (lzo_uint)(in_file->tellg()) - 12; //Subtract header size

    in_file->seekg(0, std::ios::beg);
    u32 magic = 0;

    char* readU32 = new char[4];
    in_file->read(readU32, 4);
    magic = (readU32[0] & 0xFF) | ((readU32[1] & 0xFF) << 8) | ((readU32[2] & 0xFF) << 16) | ((readU32[3] & 0xFF) << 24);
    if (magic != 0x426C5A6F)
    {
        log_s_len = sprintf(log_s, "Improper Magic"); //Malformed file
        log_file->write(log_s, log_s_len);
        in_file->close();
        return 2;
    }

    u32 out_filesize = 0;
    in_file->read(readU32, 4);
    out_filesize = (readU32[0] & 0xFF) | ((readU32[1] & 0xFF) << 8) | ((readU32[2] & 0xFF) << 16) | ((readU32[3] & 0xFF) << 24);

    u32 block_size = 0;
    in_file->read(readU32, 4);
    block_size = (readU32[0] & 0xFF) | ((readU32[1] & 0xFF) << 8) | ((readU32[2] & 0xFF) << 16) | ((readU32[3] & 0xFF) << 24);

    log_s_len = sprintf(log_s, "Decompressed filesize: %.8X - First block_size: %.8X\n", out_filesize, block_size);
    log_file->write(log_s, log_s_len);

    //Now we can read
    char* in_mem_buf = new char[in_filesize];
    for (u32 n = 0; n < in_filesize; n+= 4)
    {
        in_file->read(readU32, 4);
        in_mem_buf[n] = readU32[0];
        in_mem_buf[n+1] = readU32[1];
        in_mem_buf[n+2] = readU32[2];
        in_mem_buf[n+3] = readU32[3];
    }

    delete[] readU32;
    in_file->close();

    char* out_mem_buf = new char[out_filesize];
    lzo_memset(out_mem_buf, 0, out_filesize);
    lzo_uint decomp_size = out_filesize;

    u32 ret = 0;
    u32 block_inc = 0;
    u32 out_shift = 0;

    while (block_inc < in_filesize)
    {
        if (block_size >= 0x8000)
        {
            block_size = 0x8000;
            lzo_memcpy((unsigned char*)(out_mem_buf + out_shift), (const unsigned char*)(in_mem_buf + out_shift), block_size); //Uncompressed Data
            log_s_len = sprintf(log_s, "Copied %lu byte sector\n", (u32)block_size);
            log_file->write(log_s, log_s_len);
        }
        else if (block_size != 0)
        {
            if (block_size > 0x10000) { block_size = block_size & 0x0000FFFF; }
            ret = lzo1x_decompress((const unsigned char*)(in_mem_buf + block_inc), block_size, (unsigned char*)(out_mem_buf + out_shift), &decomp_size, NULL);
            out_shift += decomp_size;
            if (ret == LZO_E_OK)
            {
                log_s_len = sprintf(log_s, "Decompressed %lu byte sector into %lu bytes\n", (u32)block_size, (u32)decomp_size);
                log_file->write(log_s, log_s_len);
            }
            else
            {
                log_s_len = sprintf(log_s, "Decompression Error");
                log_file->write(log_s, log_s_len);
                return ret;
            }
        }

        block_inc += block_size;
        while (block_inc % 4 != 0) //align to the nearest uint32
        {
            block_inc++;
        }
        block_size = (u32)(in_mem_buf[block_inc] & 0xff) | ((in_mem_buf[block_inc+1] & 0xff) << 8) | ((in_mem_buf[block_inc+2] & 0xff) << 16) | ((in_mem_buf[block_inc+3] & 0xff) << 24);
        block_inc += 4;
        if (block_size != 0)
        {
            log_s_len = sprintf(log_s, "New block size: %.8X - Block inc: %.8X\n", block_size, block_inc + 8);
            log_file->write(log_s, log_s_len);
        }
    }

    out_file->write((char*)out_mem_buf, (u32)out_filesize);
    out_file->close();

    delete[] in_mem_buf;
    delete[] out_mem_buf;
    delete[] log_s;

    return 0;
}

int main(int argc, char* argv[])
{
    std::fstream log_file;
    char* log_string = new char[0x200];
    u32 log_len;
    log_file.open("log.txt", std::fstream::out);

    if (!log_file)
    {
        std::cout << "Error Creating Log File!" << std::endl;
        return 1;
    }

    //No working memory is needed cause we're just decompressing stuff
    log_len = sprintf(log_string, "miniLZO Version: %s - Date: %s\n", lzo_version_string(), lzo_version_date());
    log_file.write(log_string, log_len);

    if (lzo_init() != LZO_E_OK)
    {
        log_len = sprintf(log_string, "Error Initializing miniLZO!");
        log_file.write(log_string, log_len);
        return 1;
    }

    std::string in_file_path;
    std::string dir_path;
    bool dir = false;

    if (argc != 3)
    {
        std::cout << "Usage: " << argv[0] << " <[-f filename] | [-d directory_path]>";
        return 0;
    }
    else
    {
        std::string arg = argv[1];
        std::string path = argv[2];
        if (arg == "-f")
        {
            in_file_path = path;
            dir = false;
        }
        else if (arg == "-d")
        {
            dir_path = path;
            dir = true;
        }
    }

    if (dir)
    {
        std::string out_file_path;
        for (std::filesystem::directory_entry p: std::filesystem::directory_iterator(dir_path))
        {
            if (!(p.path().extension().string() == ".CGO" || p.path().extension().string() == ".DGO"))
            {
                log_file << "Skipping " << p.path().filename() << "\n";
                continue;
            }

            in_file_path = p.path().string();
            out_file_path = in_file_path;
            out_file_path = out_file_path.insert(out_file_path.length() - 3, "U");

            std::fstream in_file;
            in_file.open(in_file_path, std::fstream::in | std::fstream::binary);

            if (!in_file)
            {
                std::cout << "Error Loading File" << std::endl;
                return 1;
            }

            std::fstream out_file;
            out_file.open(out_file_path, std::fstream::out | std::fstream::binary);

            if (!out_file)
            {
                std::cout << "Error Creating Output File" << std::endl;
                return 1;
            }

            log_file << "\nFile: " << in_file_path << "\n";

            u32 ret = processFile(&in_file, &out_file, &log_file);
            in_file.close();
            out_file.close();

            if (ret != 0)
            {
                log_file << "!!!Non-Zero Return Value for File: " << in_file_path << "\n";
            }
        }
        log_file.close();
        delete[] log_string;
        return 0;
    }
    else
    {
        std::string out_file_path;
        out_file_path = in_file_path;
        out_file_path = out_file_path.insert(out_file_path.length() - 3, "U");

        std::fstream in_file;
        in_file.open(in_file_path, std::fstream::in | std::fstream::binary);

        if (!in_file)
        {
            std::cout << "Error Loading File" << std::endl;
            return 1;
        }

        std::fstream out_file;
        out_file.open(out_file_path, std::fstream::out | std::fstream::binary);

        if (!out_file)
        {
            std::cout << "Error Creating Output File" << std::endl;
            return 1;
        }

        log_file << "File: " << in_file_path << "\n";

        u32 ret = processFile(&in_file, &out_file, &log_file);
        out_file.close();
        log_file.close();
        delete[] log_string;
        return ret;
    }
    return 0;
}
