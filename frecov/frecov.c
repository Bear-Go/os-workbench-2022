#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uintptr_t uptr;
typedef int8_t i8;
// Copied from the manual
typedef struct {
  u8  BS_jmpBoot[3];
  u8  BS_OEMName[8];
  u16 BPB_BytsPerSec; // Bytes per sector
  u8  BPB_SecPerClus; // Sectors per cluster
  u16 BPB_RsvdSecCnt; // Reserved sectors
  u8  BPB_NumFATs; // Number of FATs
  u16 BPB_RootEntCnt;
  u16 BPB_TotSec16;
  u8  BPB_Media;
  u16 BPB_FATSz16; 
  u16 BPB_SecPerTrk;
  u16 BPB_NumHeads;
  u32 BPB_HiddSec;
  u32 BPB_TotSec32;
  u32 BPB_FATSz32; // Sectors per FAT
  u16 BPB_ExtFlags;
  u16 BPB_FSVer;
  u32 BPB_RootClus; // Root directory cluster
  u16 BPB_FSInfo;
  u16 BPB_BkBootSec;
  u8  BPB_Reserved[12];
  u8  BS_DrvNum;
  u8  BS_Reserved1;
  u8  BS_BootSig;
  u32 BS_VolID;
  u8  BS_VolLab[11];
  u8  BS_FilSysType[8];
  u8  __padding_1[420];
  u16 Signature_word;
} __attribute__((packed)) fat32hdr;

typedef struct {
  u8  DIR_Name[8];
  u8  DIR_Suffix[3];
  u8  DIR_Attr;
  u8  DIR_NTRes;
  u8  DIR_CrtTimeTenth;
  u16 DIR_CrtTime;
  u16 DIR_CrtDate;
  u16 DIR_LstAccDate;
  u16 DIR_FstClusHI;
  u16 DIR_WrtTime;
  u16 DIR_WrtDate;
  u16 DIR_FstClusLO;
  u32 DIR_FileSize;
} __attribute__((packed)) dirhdr;

typedef struct {
  u8  LDIR_Ord;
  u16  LDIR_Name1[5];
  u8  LDIR_Attr;
  u8  LDIR_Type;
  u8  LDIR_Chksum;
  u16  LDIR_Name2[6];
  u16 LDIR_FstClusLO;
  u16  LDIR_Name3[2];
} __attribute__((packed)) ldirhdr;

typedef struct {
  u8 data[0];
  u8 bfType[2];
  u32 bfSize;
  u16 bfReserved1;
  u16 bfReserved2;
  u32 bfOffBits;
  u32 biSize;
  u32 biWidth;
  u32 biHeight;
} __attribute__((packed)) bmphdr;

void *map_disk(const char *fname);

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s fs-image\n", argv[0]);
    exit(1);
  }

  setbuf(stdout, NULL);

  assert(sizeof(fat32hdr) == 512); // defensive
  assert(sizeof(dirhdr) == 32); // defensive
  assert(sizeof(ldirhdr) == 32); // defensive
  assert(sizeof(bmphdr) == 26); // defensive

  // map disk image to memory
  fat32hdr *hdr = map_disk(argv[1]);

#define RRS (hdr->BPB_RsvdSecCnt)
#define FRS (hdr->BPB_NumFATs * hdr->BPB_FATSz32)
#define RDRS ((hdr->BPB_RootClus - 2) * hdr->BPB_SecPerClus)
#define BPS (hdr->BPB_BytsPerSec)

  uptr data_offset = (RRS + FRS + RDRS) * BPS;
  uptr data_size = hdr->BPB_TotSec32 * BPS;

  uptr cluster_size = hdr->BPB_SecPerClus * BPS;
  uptr data_start = (uptr)hdr + data_offset;
  uptr data_end = data_start + data_size;

  for (uptr ptr = data_start; ptr < data_end; ptr += cluster_size) {
    dirhdr *dir = (dirhdr *)ptr;
    dirhdr *end = (dirhdr *)(ptr + cluster_size);
    int BMP_cnt = 0;
    for (; dir < end; dir++) {
      if (strncmp((char *)dir->DIR_Suffix, "BMP", 3) == 0) {
        BMP_cnt++;
      }
    }
    if (BMP_cnt >= 3) {
      // this is directory file
      dir = (dirhdr *)ptr;
      end = (dirhdr *)(ptr + cluster_size);
      char filename[128];
      for (; dir < end; dir++) {
        if (dir->DIR_Name[0] == 0x00 || dir->DIR_Name[0] == 0xe5) {
          continue;
        }
        if (strncmp((char *)dir->DIR_Suffix, "BMP", 3) == 0 && dir->DIR_Name[6] == '~') {
          memset(filename, '\0', sizeof(filename));
          ldirhdr *ldir = (ldirhdr *)dir;
          ldir--;
          if (ldir->LDIR_Attr == 0x0f) { 
            for (int idx = 0; ldir->LDIR_Attr == 0x0f; ldir--) {
              for (int i = 0; i < 5; i++) {
                filename[idx++] = ldir->LDIR_Name1[i];
              }
              for (int i = 0; i < 6; i++) {
                filename[idx++] = ldir->LDIR_Name2[i];
              }
              for (int i = 0; i < 2; i++) {
                filename[idx++] = ldir->LDIR_Name3[i];
              }
            }
          } else {
            int len_t = 0;
            for (int i = 0; i < 8; i++) {
              if (dir->DIR_Name[i] == ' ') {
                break;
              }
              filename[len_t++] = dir->DIR_Name[i];
            }
            filename[len_t++] = '.';
            filename[len_t++] = 'b';
            filename[len_t++] = 'm';
            filename[len_t++] = 'p';
          }
          int len = strlen(filename);
          if (len >= 5 && filename[len - 4] == '.') {
            // a bmp file is found
            u32 offset = (u32)dir->DIR_FstClusHI << 16 | dir->DIR_FstClusLO;
            u32 offset_addr = (offset - 2) * cluster_size;
            bmphdr *bmp_ptr = (bmphdr *)((uptr)hdr + (RRS + FRS) * BPS + offset_addr);
            if (strncmp((char *)bmp_ptr->bfType, "BM", 2) == 0) {
              char path[256];
              sprintf(path, "%s/%s", "/tmp", filename);
              int fd = open(path, O_WRONLY | O_CREAT, 0644);
              if (fd < 0) {
                perror("open");
                exit(1);
              }
              
              void *cur = (void *)bmp_ptr;
              void *end = (void *)((uptr)bmp_ptr + bmp_ptr->bfSize);
              write(fd, cur, cluster_size);
              int pixel_cnt = (cluster_size - bmp_ptr->bfOffBits - 1) / 3;
              void *last_pixel = (void *)((uptr)bmp_ptr + bmp_ptr->bfOffBits + pixel_cnt * 3);
              void *prev = (void *)((uptr)bmp_ptr + cluster_size);
              cur += cluster_size;
              
              while (cur < end) {
                // is cur cluster null
                int bitinprev = prev - last_pixel;
                assert(bitinprev <= 3);
                int bitincur = 6 - bitinprev;
                u8 color_check[6];
                memset(color_check, '\0', sizeof(color_check));
                for (int i = 0; i < bitinprev; i++) {
                  color_check[i] = *(u8 *)(last_pixel + i);
                }
                for (int i = 0; i < bitincur; i++) {
                  color_check[bitinprev + i] = *(u8 *)(cur + i);
                }
                int diff = color_check[0] - color_check[3];
                diff += color_check[1] - color_check[4];
                diff += color_check[2] - color_check[5];
                int r = rand() % 10;
                if (diff == 0 && r == 0) {
                  // this is a null cluster
                  cur += cluster_size;
                  continue;
                }
                prev = cur + cluster_size;
                int pcnt = (cluster_size + bitinprev - 1) / 3;
                last_pixel = (void *)((uptr)cur + pcnt * 3 - bitinprev);
                int ret = write(fd, cur, cluster_size);
                assert(ret == cluster_size);
                cur += cluster_size;
              }



              // write(fd, bmp_ptr, bmp_ptr->bfSize);

              close(fd);
              char cmd[512];
              sprintf(cmd, "sha1sum %s", path);
              FILE *fp = popen(cmd, "r");
              if (fp == NULL) {
                perror("popen");
                exit(1);
              }
              char sha1[41];
              fscanf(fp, "%s", sha1);
              pclose(fp);
              printf("%s %s\n", sha1, filename);
            }
          }
        }
      }
    }

  }

  // file system traversal
  munmap(hdr, hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec);
  return 0;
}

void *map_disk(const char *fname) {
  int fd = open(fname, O_RDWR);

  if (fd < 0) {
    perror(fname);
    goto release;
  }

  off_t size = lseek(fd, 0, SEEK_END);
  if (size == -1) {
    perror(fname);
    goto release;
  }

  fat32hdr *hdr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (hdr == (void *)-1) {
    goto release;
  }

  close(fd);

  if (hdr->Signature_word != 0xaa55 ||
      hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec != size) {
    fprintf(stderr, "%s: Not a FAT file image\n", fname);
    goto release;
  }
  return hdr;

release:
  if (fd > 0) {
    close(fd);
  }
  exit(1);
}