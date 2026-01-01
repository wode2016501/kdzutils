/*
 * 由千问ai生成 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h> // 需要链接 zlib 库 (-lz)

// 结构体定义：对应 Python 中的 dz_sub_dict
typedef struct {
    char header[5];      // 4s + terminator
    char type[33];       // 32s + terminator
    char name[65];       // 64s + terminator
    uint32_t unknown;
    uint32_t length;
    char checksum[17];   // 16s + terminator
    uint32_t spacer1;
    uint32_t spacer2;
    uint32_t spacer3;
    // Pad 被忽略，因为我们只关心有用的数据
    uint32_t offset;     // 在 Python 中动态添加，这里也包含进来
} DZPartitionEntry;

// 全局常量
#define DZ_HEADER "\x32\x96\x18\x74"
#define DZ_SUB_HEADER "\x30\x12\x95\x78"
#define DZ_SUB_LEN 512
#define MAX_PARTITIONS 64
#define DEFAULT_OUTDIR "dzextracted"

// 函数声明
void printUsage(char *progName);
void listPartitions(DZPartitionEntry *parts, int count);
void extractPartition(FILE *inFile, DZPartitionEntry *part, const char *outDir);

int main(int argc, char *argv[]) {
    // 参数变量
    char *dz_filename = NULL;
    int list_only = 0;
    int extract_all = 0;
    int extract_id = -1;
    char *output_dir = DEFAULT_OUTDIR;

    // 文件与解析变量
    FILE *fp = NULL;
    DZPartitionEntry partitions[MAX_PARTITIONS];
    int partition_count = 0;
    char header_check[5];
    uint32_t file_length;
    int i;

    // 1. 手动解析参数
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
            if (i + 1 < argc) dz_filename = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_only = 1;
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--extract") == 0) {
            extract_all = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--single") == 0) {
            if (i + 1 < argc) extract_id = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) {
            if (i + 1 < argc) output_dir = argv[++i];
        }
    }

    if (!dz_filename || !(list_only || extract_all || extract_id >= 0)) {
        printUsage(argv[0]);
        return 1;
    }

    // 2. 打开文件
    fp = fopen(dz_filename, "rb");
    if (!fp) {
        printf("[-] Error: Cannot open file %s\n", dz_filename);
        return 1;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    file_length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 3. 验证 DZ 主 Header
    fread(header_check, 1, 4, fp);
    header_check[4] = '\0';

    if (memcmp(header_check, DZ_HEADER, 4) != 0) {
        printf("[!] Error: Unsupported DZ file format.\n");
        fclose(fp);
        return 1;
    }

    // 跳过剩余的 header (到 512 字节处)
    fseek(fp, 512, SEEK_SET);

    // 4. 解析分区表 (getPartitions)
    printf("[+] Reading DZ partition table...\n");

    while (1) {
        char buf[DZ_SUB_LEN];
        DZPartitionEntry temp_part;
        size_t bytesRead = fread(buf, 1, DZ_SUB_LEN, fp);

        if (bytesRead < DZ_SUB_LEN) break;

        // 解包数据 (对应 struct.unpack)
        memcpy(temp_part.header, buf, 4);
        temp_part.header[4] = '\0';

        // 验证子 Header
        if (memcmp(temp_part.header, DZ_SUB_HEADER, 4) != 0) {
            printf("[!] Bad DZ sub header at offset %ld!\n", ftell(fp) - DZ_SUB_LEN);
            break;
        }

        memcpy(temp_part.type, buf + 4, 32);
        temp_part.type[32] = '\0';
        // 去除填充的 \0 (Collapse)
        temp_part.type[strcspn(temp_part.type, "\0")] = 0;

        memcpy(temp_part.name, buf + 36, 64);
        temp_part.name[64] = '\0';
        temp_part.name[strcspn(temp_part.name, "\0")] = 0;

        memcpy(&temp_part.unknown, buf + 100, 4);
        memcpy(&temp_part.length, buf + 104, 4);
        memcpy(&temp_part.checksum, buf + 108, 16);
        temp_part.checksum[16] = '\0';
        memcpy(&temp_part.spacer1, buf + 124, 4);
        memcpy(&temp_part.spacer2, buf + 128, 4);
        memcpy(&temp_part.spacer3, buf + 132, 4);

        // 设置数据偏移 (当前文件位置)
        temp_part.offset = ftell(fp);

        partitions[partition_count] = temp_part;
        partition_count++;

        // 检查是否到达文件末尾
        if (temp_part.offset + temp_part.length >= file_length) {
            break;
        }

        // 移动到下一个块的起始位置
        fseek(fp, temp_part.length, SEEK_CUR);

        if (partition_count >= MAX_PARTITIONS) break;
    }

    printf("[+] Found %d partitions.\n\n", partition_count);

    // 5. 执行命令
    if (list_only) {
        listPartitions(partitions, partition_count);
    } 
    else if (extract_id >= 0) {
        if (extract_id < partition_count) {
            extractPartition(fp, &partitions[extract_id], output_dir);
        } else {
            printf("[-] Error: Partition ID out of range.\n");
        }
    }
    else if (extract_all) {
        for (i = 0; i < partition_count; i++) {
            extractPartition(fp, &partitions[i], output_dir);
        }
    }

    fclose(fp);
    return 0;
}

void listPartitions(DZPartitionEntry *parts, int count) {
    printf("[+] DZ Partition List\n=========================================\n");
    for (int i = 0; i < count; i++) {
        printf("%2d : %s (%u bytes, Type: %s)\n", i, parts[i].name, parts[i].length, parts[i].type);
    }
}

void extractPartition(FILE *inFile, DZPartitionEntry *part, const char *outDir) {
    char outPath[512];
    FILE *outFile;
    Bytef *comp_data;
    Bytef *uncomp_data;
    uLongf uncomp_size;

    // 构建输出路径
    snprintf(outPath, sizeof(outPath), "%s/%s", outDir, part->name);
    printf("[+] Extracting %s -> %s\n", part->name, outPath);

    // 移动文件指针到数据偏移处
    fseek(inFile, part->offset, SEEK_SET);

    // 分配内存
    comp_data = (Bytef*)malloc(part->length);
    if (!comp_data) {
        printf("[-] Error: Memory allocation failed for compressed data.\n");
        return;
    }

    // 读取压缩数据
    fread(comp_data, 1, part->length, inFile);

    // ************************************************************
    // ZLIB 解压缩逻辑
    // 由于我们不知道解压后的大小，先尝试分配一个较大的缓冲区
    // (或者可以通过读取分区表中的信息获取，但这里为了通用性，使用 zlib 的流式或猜测大小)
    // 这里假设解压后大小是压缩大小的 4 倍 (保守估计，实际通常更大)
    // ************************************************************
    uncomp_size = part->length * 4+1024*1024*50;
    uncomp_data = (Bytef*)malloc(uncomp_size);

    if (!uncomp_data) {
        printf("[-] Error: Memory allocation failed for uncompressed data.\n");
        free(comp_data);
        return;
    }

    // 执行解压
    int ret = uncompress(uncomp_data, &uncomp_size, comp_data, part->length);
    if (ret != Z_OK) {
        printf("[-] Error: Zlib decompression failed (%d). Trying to write raw data.\n", ret);
        // 如果解压失败（可能不是 zlib 格式），直接写入原始数据
	sprintf(outPath,"%s.gz",outPath);
        outFile = fopen(outPath, "wb");
        if (outFile) {
            fwrite(comp_data, 1, part->length, outFile);
            fclose(outFile);
            printf("[+] Raw data saved.\n");
        }
        free(comp_data);
        free(uncomp_data);
        return;
    }

    // 创建输出目录（简单处理）
    // 注意：Windows 下可能需要 system("mkdir ...") 或 _mkdir
    // 这里假设目录已存在或 Linux 环境
    outFile = fopen(outPath, "wb");
    if (outFile) {
        fwrite(uncomp_data, 1, uncomp_size, outFile);
        fclose(outFile);
        printf("[+] Success: %s (%lu bytes written)\n", part->name, uncomp_size);
    } else {
        printf("[-] Error: Cannot create output file %s\n", outPath);
    }

    // 释放内存
    free(comp_data);
    free(uncomp_data);
}

void printUsage(char *progName) {
    printf("LG Compressed DZ File Extractor (C Version)\n");
    printf("Usage: %s -f <file.dz> [OPTIONS]\n\n", progName);
    printf("Required:\n");
    printf("  -f, --file <file>     Specify DZ file\n");
    printf("  One of the following:\n");
    printf("    -l, --list          List partitions\n");
    printf("    -x, --extract       Extract all partitions\n");
    printf("    -s, --single <id>   Extract single partition by ID\n");
    printf("Options:\n");
    printf("  -o, --out <dir>       Output directory (default: dzextracted)\n");
}


