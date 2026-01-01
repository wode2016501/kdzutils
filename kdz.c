/*
 * 由千问ai生成
 */ 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// 定义分区信息结构体 (对应 Python 中的字典项)
typedef struct {
    char name[33];      // 32字节 + 1字节结束符
    uint32_t length;    // 分区数据长度
    uint32_t offset;    // 数据在文件中的偏移
    // 注意：为了简单，忽略未使用的字段(pad, unknow1, unknow2)
} PartitionEntry;

// 全局常量定义
#define KDZ_HEADER "\x28\x05\x00\x00\x34\x31\x25\x80"
#define KDZ_SUB_LEN 272
#define MAX_PARTITIONS 64 // 假设最大分区数量
#define DEFAULT_OUTPUT_DIR "kdzextracted"

// 函数声明
void printUsage(char *progName);
void listPartitions(PartitionEntry *parts, int count);
void extractPartition(FILE *inFile, PartitionEntry *part, const char *outDir);
void safeStringCopy(char *dest, const char *src, size_t size);

int main(int argc, char *argv[]) {
    // 参数变量
    char *kdz_filename = NULL;
    int list_only = 0;
    int extract_all = 0;
    int extract_id = -1;
    char *output_dir = DEFAULT_OUTPUT_DIR;

    // 临时变量
    FILE *fp = NULL;
    PartitionEntry partitions[MAX_PARTITIONS];
    int partition_count = 0;
    char header_check[9]; // 8字节 + 结束符
    long current_offset;
    int i;

    // 1. 简单的参数解析 (C语言标准库没有 argparse，需手动解析)
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
            if (i + 1 < argc) kdz_filename = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_only = 1;
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--extract") == 0) {
            extract_all = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--single") == 0) {
            if (i + 1 < argc) {
                extract_id = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) {
            if (i + 1 < argc) output_dir = argv[++i];
        }
    }

    // 验证必要参数
    if (kdz_filename == NULL || (!(list_only || extract_all || (extract_id >= 0)))) {
        printUsage(argv[0]);
        return 1;
    }

    // 2. 打开文件
    fp = fopen(kdz_filename, "rb");
    if (!fp) {
        printf("[-] Error: Cannot open file %s\n", kdz_filename);
        return 1;
    }

    // 3. 验证头部
    fread(header_check, 1, 8, fp);
    header_check[8] = '\0'; // 确保字符串结束

    if (memcmp(header_check, KDZ_HEADER, 4) != 0) {
        printf("[!] Error: Unsupported KDZ file format.\n");
        printf("[ ] Expected: 28 05 00 00 34 31 25 80\n");
        printf("[ ] Received: ");
        for (int j = 0; j < 8; j++) {
            printf("%02X ", (unsigned char)header_check[j]);
        }
        printf("\n");
        fclose(fp);
        return 1;
    }

    // 4. 解析分区表 (getPartitions)
    printf("[+] Reading partition table...\n");
    
    while (1) {
        char buf[KDZ_SUB_LEN];
        PartitionEntry temp_part;

        // 读取一个分区头
        size_t bytesRead = fread(buf, 1, KDZ_SUB_LEN, fp);
        if (bytesRead < KDZ_SUB_LEN) break; // 文件结束

        // 解包数据 (struct.unpack 的模拟)
        // name: 32s (复制并截断 \x00)
        memcpy(temp_part.name, buf, 32);
        temp_part.name[32] = '\0'; // 确保结束
        // 手动去除末尾的 \0 填充 (Collapse)
        char *null_pos = strchr(temp_part.name, '\0');
        if (null_pos) *null_pos = '\0';

        // length: I (小端序，假设文件是小端序，通常PC是小端)
        memcpy(&temp_part.length, buf + 256, 4); // 32(name)+224(pad) = 256
        // offset: I
        memcpy(&temp_part.offset, buf + 264, 4);

        // 存储到数组
        partitions[partition_count] = temp_part;
        partition_count++;

        // 检查下一个头是否存在 (读取4字节标记)
        current_offset = ftell(fp);
        char mark[4];
        if (fread(mark, 1, 4, fp) != 4) break;
        
        if (mark[0] == 0x00 && mark[1] == 0x00 && mark[2] == 0x00 && mark[3] == 0x00) {
            break; // 结束标记
        } else {
            // 回退4字节，以便下次循环正确读取头
            fseek(fp, current_offset, SEEK_SET);
        }

        // 防止数组越界
        if (partition_count >= MAX_PARTITIONS) break;
    }

    printf("[+] Found %d partitions.\n\n", partition_count);

    // 5. 根据参数执行命令
    if (list_only) {
        listPartitions(partitions, partition_count);
    } 
    else if (extract_id >= 0) {
        if (extract_id < partition_count) {
            printf("[+] Extracting single partition: %s (ID: %d)\n", partitions[extract_id].name, extract_id);
            extractPartition(fp, &partitions[extract_id], output_dir);
        } else {
            printf("[-] Error: Partition ID %d out of range (0-%d).\n", extract_id, partition_count - 1);
        }
    }
    else if (extract_all) {
        printf("[+] Extracting all partitions to %s...\n", output_dir);
        for (i = 0; i < partition_count; i++) {
            extractPartition(fp, &partitions[i], output_dir);
        }
    }

    fclose(fp);
    return 0;
}

// 列出分区信息
void listPartitions(PartitionEntry *parts, int count) {
    printf("[+] KDZ Partition List\n=========================================\n");
    for (int i = 0; i < count; i++) {
        printf("%2d : %s (%u bytes)\n", i, parts[i].name, parts[i].length);
    }
}

// 提取单个分区
void extractPartition(FILE *inFile, PartitionEntry *part, const char *outDir) {
    char outPath[512];
    FILE *outFile;
    char buffer[1024];
    size_t total_read = 0;
    size_t to_read;

    // 构建输出路径
    snprintf(outPath, sizeof(outPath), "%s/%s", outDir, part->name);

    // 尝试创建目录 (简单处理，如果目录不存在，写入可能会失败)
    // 在实际应用中，你可能需要调用 mkdir 或 system("mkdir ...")
    printf("[+] Extracting %s -> %s\n", part->name, outPath);

    // 移动输入文件指针到数据偏移处
    fseek(inFile, part->offset, SEEK_SET);

    // 打开输出文件
    outFile = fopen(outPath, "wb");
    if (!outFile) {
        printf("[-] Error: Cannot create file %s\n", outPath);
        return;
    }

    // 分块写入
    while (total_read < part->length) {
        to_read = (part->length - total_read) > sizeof(buffer) ? sizeof(buffer) : (part->length - total_read);
        size_t read_now = fread(buffer, 1, to_read, inFile);
        
        if (read_now == 0) break;

        fwrite(buffer, 1, read_now, outFile);
        total_read += read_now;
    }

    fclose(outFile);
    printf("[+] Done: %s\n", part->name);
}

// 打印使用方法
void printUsage(char *progName) {
    printf("LG KDZ File Extractor (C Version)\n");
    printf("Usage: %s -f <file.kdz> [OPTIONS]\n\n", progName);
    printf("Required:\n");
    printf("  -f, --file <file>     Specify KDZ file\n");
    printf("  One of the following:\n");
    printf("    -l, --list          List partitions\n");
    printf("    -x, --extract       Extract all partitions\n");
    printf("    -s, --single <id>   Extract single partition by ID\n");
    printf("Options:\n");
    printf("  -o, --out <dir>       Output directory (default: kdzextracted)\n");
}

// 安全字符串复制辅助函数 (如果需要处理路径拼接等)
void safeStringCopy(char *dest, const char *src, size_t size) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}


