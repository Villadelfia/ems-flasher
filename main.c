#include <err.h>
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ems.h"

#define VERSION "0.05"

// one bank is 32 megabits
#define BANK_SIZE 0x400000
#define SRAM_SIZE 0x020000

const int limits[3] = {0, BANK_SIZE, SRAM_SIZE};

// operation mode
#define MODE_READ   1
#define MODE_WRITE  2
#define MODE_TITLE  3

/* options */
typedef struct _options_t {
    int verbose;
    int blocksize;
    int mode;
    char *file;
    int bank;
    int space;
} options_t;

// defaults
options_t opts = {
    .verbose            = 0,
    .blocksize          = 0,
    .mode               = 0,
    .file               = NULL,
    .bank               = 0,
    .space              = 0,
};

// NXXXXXXX logo ;)
const char nintylogo[0x30] =
   {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E};

//offsets to parts of the cart header
enum headeroffsets {
    HEADER_LOGO = 0x104,
    HEADER_TITLE = 0x134,
    HEADER_CGBFLAG = 0x143,
    HEADER_SGBFLAG = 0x146,
    HEADER_ROMSIZE = 0x148,
    HEADER_RAMSIZE = 0x149,
    HEADER_REGION = 0x14A,
    HEADER_OLDLICENSEE = 0x14B,
    HEADER_ROMVER = 0x14C,
    HEADER_CHKSUM = 0x14D,
};

// default blocksizes
#define BLOCKSIZE_READ  4096
#define BLOCKSIZE_WRITE 32

/**
 * Usage
 */
void usage(char *name) {
    printf("Usage: %s < --read | --write > <file>\n", name);
    printf("       %s --title\n", name);
    printf("       %s --version\n", name);
    printf("       %s --help\n", name);
    printf("Writes a ROM or SAV file to the EMS 64 Mbit USB flash cart\n\n");
    printf("Options:\n");
    printf("    --read                  read entire cart into file\n");
    printf("    --write                 write ROM file to cart\n");
    printf("    --title                 title of the ROM in both banks\n");
    printf("    --bank <num>            select cart bank (1 or 2)\n");
    printf("    --save                  force write to SRAM\n");
    printf("    --rom                   force write to Flash ROM\n");
    printf("\n");
    printf("You MUST supply exactly one of --read, --write, or --title\n");
    printf("Reading or writing with a file ending in .sav will write to SRAM.\n");
    printf("To select between ROM and SRAM, use ONE of the --save / --rom options.\n");
    printf("\n");
    printf("Advanced options:\n");
    printf("    --blocksize <size>      bytes per block (default: 4096 read, 32 write)\n");
    exit(1);
}

/**
 * Get the options to the binary. Options are stored in the global "opts".
 */
void get_options(int argc, char **argv) {
    int c, optval;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"help", 0, 0, 'h'},
            {"version", 0, 0, 'V'},
            {"verbose", 0, 0, 'v'},
            {"read", 0, 0, 'r'},
            {"write", 0, 0, 'w'},
            {"title", 0, 0, 't'},
            {"blocksize", 1, 0, 's'},
            {"bank", 1, 0, 'b'},
            {"save", 0, 0, 'S'},
            {"rom", 0, 0, 'R'},
            {0, 0, 0, 0}
        };

        c = getopt_long(
            argc, argv, "hVvs:rwt",
            long_options, &option_index
        );
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                usage(argv[0]);
                break;
            case 'V':
                printf("EMS-flasher %s\n", VERSION);
                exit(0);
                break;
            case 'v':
                opts.verbose = 1;
                break;
            case 'r':
                if (opts.mode != 0) goto mode_error;
                opts.mode = MODE_READ;
                break;
            case 'w':
                if (opts.mode != 0) goto mode_error;
                opts.mode = MODE_WRITE;
                break;
            case 't':
                if (opts.mode != 0) goto mode_error;
                opts.mode = MODE_TITLE;
                break;
            case 's':
                optval = atoi(optarg);
                if (optval <= 0) {
                    printf("Error: block size must be > 0\n");
                    usage(argv[0]);
                }
                // TODO make sure it divides evenly into bank size
                opts.blocksize = optval;
                break;
            case 'b':
                optval = atoi(optarg);
                if (optval < 1 || optval > 2) {
                    printf("Error: cart only has two banks: 1 and 2\n");
                    usage(argv[0]);
                }
                opts.bank = optval - 1;
                break;
            case 'S':
                if (opts.space != 0) goto mode_error2;
                opts.space = FROM_SRAM;
                break;
            case 'R':
                if (opts.space != 0) goto mode_error2;
                opts.space = FROM_ROM;
                break;
            default:
                usage(argv[0]);
                break;
        }
    }

    // user didn't supply mode
    if (opts.mode == 0)
        goto mode_error;

    if (opts.mode == MODE_WRITE || opts.mode == MODE_READ) {
        // user didn't give a filename
        if (optind >= argc) {
            printf("Error: you must provide an %s filename\n", opts.mode == MODE_READ ? "output" : "input");
            usage(argv[0]);
        }

        // extra argument: ROM file
        opts.file = argv[optind];

        // set a default blocksize if the user hasn't given one
        if (opts.blocksize == 0)
            opts.blocksize = opts.mode == MODE_READ ? BLOCKSIZE_READ : BLOCKSIZE_WRITE;
    }

    return;

mode_error:
    printf("Error: must supply exactly one of --read, --write, or --title\n");
    usage(argv[0]);

mode_error2:
    printf("Error: must supply zero or one of --save, or --rom\n");
    usage(argv[0]);
}

/**
 * header_info
 */
void header_info(unsigned char* buf) {
    int i;
    int willboot = 2;

    printf("\tTitle: ");
    if(buf[HEADER_TITLE] == 0)
        printf("NONE");
    for(i = HEADER_TITLE; i < (HEADER_TITLE + 16); i++) {
        if(buf[i] == 0)
            break;
        putchar(buf[i]);
    }
    printf("\n");

    printf("\tNintendo logo: ");
    for(i = 0; i < 0x30; ++i) {
        if((unsigned char) nintylogo[i] != buf[HEADER_LOGO + i])
            break;
    }
    if(i == 0x30) {
        printf("PASS\n");
    } else if(i > 0x18) {
        printf("FAIL, but will boot on CGB\n");
        willboot = 1;
    } else {
        printf("FAIL\n");
        willboot = 0;
    }

    printf("\tHardware support: ");
    if ((buf[HEADER_CGBFLAG] & 128) && (buf[HEADER_CGBFLAG] & 64)) {
        printf("CGB\n");
    } else if ((buf[HEADER_CGBFLAG] & 128) && (buf[HEADER_CGBFLAG] & 64) && buf[HEADER_SGBFLAG] == 0x03) {
        printf("CGB <+SGB>, not real option set\n");
    } else if ((buf[HEADER_CGBFLAG] & 128) && buf[HEADER_SGBFLAG] == 0x03) {
        printf("DMG <+CGB, +SGB>\n");
    } else if ((buf[HEADER_CGBFLAG] & 128)) {
        printf("DMG <+CGB>\n");
    } else if (buf[HEADER_SGBFLAG] == 0x03) {
        printf("DMG <+SGB>\n");
    } else {
        printf("DMG\n");
    }

    printf("\tHeader checksum: ");
    uint8_t calculated_chk = 0;
    for (i = HEADER_TITLE; i <= HEADER_CHKSUM; ++i) {
        calculated_chk += buf[i];
    }
    calculated_chk += 25;

    if (calculated_chk != 0) {
        printf("FAIL\n");
        willboot = 0;
    } else {
        printf("PASS\n");
    }

    printf("\tRom size: ");
    switch (buf[HEADER_ROMSIZE]) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            printf("%u KB ROM\n", 32 << buf[HEADER_ROMSIZE]);
            break;
        case 0x52:
            printf("1152 KB ROM\n");
            break;
        case 0x53:
            printf("1280 KB ROM\n");
            break;
        case 0x54:
            printf("1536 KB ROM\n");
            break;
        default:
            printf("Unknown ROM size code\n");
            break;
    }

    printf("\tBoot status: ");
    switch(willboot) {
        case 0:
            printf("This game will not boot on any system.\n");
            break;
        case 1:
            printf("This game will only boot on CGB.\n");
            break;
        default:
            printf("This game will work on any system.\n");
            break;
    }   
}

/**
 * Main
 */
int main(int argc, char **argv) {
    int r;
    setbuf(stdout, NULL);

    get_options(argc, argv);

    // Force verbose.
    opts.verbose = 1;

    if (opts.verbose)
        printf("Trying to find EMS cart\n");

    r = ems_init();
    if (r < 0)
        return 1;

    if (opts.verbose)
        printf("Claimed EMS cart\n");

    // we'll need a buffer one way or another
    int blocksize = opts.blocksize;
    uint32_t offset = 0;
    uint32_t base = opts.bank * BANK_SIZE;
    if (opts.verbose)
        printf("Base address is 0x%X\n", base);
    
    unsigned char *buf = malloc(blocksize);
    if (buf == NULL)
        err(1, "malloc");

    // determine what we're reading/writing from/to
    int space = opts.space;
    if (space == 0 && opts.file != NULL) {
        //attempt to autodetect the file
        //are the last four characters .sav ?
        size_t namelen = strlen(opts.file);

        if (opts.file[namelen - 4] == '.' &&
            tolower(opts.file[namelen - 3]) == 's' &&
            tolower(opts.file[namelen - 2]) == 'a' &&
            tolower(opts.file[namelen - 1]) == 'v') {
            space = FROM_SRAM;
        } else {
            space = FROM_ROM;
        }
    }

    // read the ROM and save it into the file
    if (opts.mode == MODE_READ) {
        FILE *save_file = fopen(opts.file, "w");
        if (save_file == NULL)
            err(1, "Can't open %s for writing", opts.file);

        if (opts.verbose && space == FROM_ROM)
            printf("Saving ROM into %s\n", opts.file);
        else if (opts.verbose)
            printf("Saving SAVE into %s\n", opts.file);

        unsigned int readuntil = limits[space];
        int untilset = 0;
        while ((int)(offset + blocksize) <= readuntil) {
            r = ems_read(space, offset + base, buf, blocksize);
            if (r < 0) {
                warnx("Can't read %d bytes at offset %u\n", blocksize, offset);
                return 1;
            }

            r = fwrite(buf, blocksize, 1, save_file);
            if (r != 1)
                err(1, "Can't write %d bytes into file at offset %u", blocksize, offset);

            offset += blocksize;
            printf("Saving: %.2f%%\r", ((float) offset / (float) readuntil) * 100);

            if(offset >= HEADER_ROMSIZE && untilset == 0 && space == FROM_ROM) {
                switch(buf[HEADER_ROMSIZE - (offset - blocksize)]) {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                        readuntil = (32 << buf[HEADER_ROMSIZE - (offset - blocksize)]) * 1024;
                        break;
                    case 0x52:
                        readuntil = 1152 * 1024;
                        break;
                    case 0x53:
                        readuntil = 1280 * 1024;
                        break;
                    case 0x54:
                        readuntil = 1536 * 1024;
                        break;
                    default:
                        break;
                }
                untilset = 1;
            }
        }

        fclose(save_file);

        if (opts.verbose)
            printf("Successfully wrote %u bytes into %s\n", offset, opts.file);
    }

    // write ROM in the file to bank 1
    else if (opts.mode == MODE_WRITE) {
        FILE *write_file = fopen(opts.file, "r");
        if (write_file == NULL) {
            if (space == TO_ROM)
                err(1, "Can't open ROM file %s", opts.file);
            else
                err(1, "Can't open SAVE file %s", opts.file);
        }

        fseek(write_file, 0L, SEEK_END);
        int size = ftell(write_file);
        rewind(write_file);

        if(size > 4*1024*1024 && space == TO_ROM) {
            err(1, "ROM file %s is %d bytes large, max is %d", opts.file, size, (4*1024*1024));
        } else if(size > 128*1024 && space == TO_SRAM) {
            err(1, "SAVE file %s is %d bytes large, max is %d", opts.file, size, (128*1024));
        }

        if (opts.verbose && space == TO_ROM)
            printf("Writing ROM file %s\n", opts.file);
        else if (opts.verbose)
            printf("Writing SAVE file %s\n", opts.file);

        while ((int)(offset + blocksize) <= limits[space] && fread(buf, blocksize, 1, write_file) == 1) {
            r = ems_write(space, offset + base, buf, blocksize);
            if (r < 0) {
                warnx("Can't write %d bytes at offset %u", blocksize, offset);
                return 1;
            }

            offset += blocksize;
            printf("Writing: %.2f%%\r", ((float) offset / (float) size) * 100);
        }

        fclose(write_file);

        if (opts.verbose)
            printf("Successfully wrote %u bytes from %s\n", offset, opts.file);
    }

    // read the ROM header
    else if (opts.mode == MODE_TITLE) {
        unsigned char buf[512];

        r = ems_read(FROM_ROM, 0, buf, 512);
        if (r < 0)
            errx(1, "Couldn't read ROM header at bank 0, offset 0, len 512\n");
        
        printf("Bank 0: \n");
        header_info(buf);

        // readability.
        printf("\n");

        r = ems_read(FROM_ROM, BANK_SIZE, (unsigned char *)buf, 512);
        if (r < 0)
            errx(1, "Couldn't read ROM header at bank 1, offset 0, len 512\n");
        
        printf("Bank 1: \n");
        header_info(buf);
    }

    // should never reach here
    else
        errx(1, "Unknown mode %d, file a bug report", opts.mode);

    // belt and suspenders
    free(buf);

    return 0;
}
