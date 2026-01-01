#由千问ai生成
import os
import argparse
import sys
from struct import *
from collections import OrderedDict

class KDZFileTools:
    """
    LGE KDZ File tools
    """

    # Setup variables
    partitions = []
    outdir = "kdzextracted"
    infile = None

    # 注意：Python 3 中 bytes literals 需要加 b 前缀
    kdz_header = b'\x28\x05\x00\x00\x34\x31\x25\x80'
    kdz_sub_len = 272

    # Format string dict
    #   itemName is the new dict key for the data to be stored under
    #   formatString is the Python formatstring for struct.unpack()
    #   collapse is boolean that controls whether extra \x00 's should be stripped
    # Example:
    #   ('itemName', ('formatString', collapse))
    kdz_sub_dict = OrderedDict([
      ('name'   , ('32s', True)),
      ('pad'    , ('224s', True)),
      ('length' , ('I', False)),
      ('unknow1', ('I', False)),
      ('offset' , ('I', False)),
      ('unknow2', ('I', False))
      ])

    # Generate the formatstring for struct.unpack()
    kdz_formatstring = " ".join([x[0] for x in kdz_sub_dict.values()])

    # Generate list of items that can be collapsed (truncated)
    # 注意：Python 3 中 zip 返回迭代器，转为 list 以便多次使用或保持逻辑一致
    kdz_collapsibles = list(zip(kdz_sub_dict.keys(), [x[1] for x in kdz_sub_dict.values()]))

    def readKDZHeader(self):
        """
        Reads the KDZ header, and returns a single kdz_item
        in the form as defined by self.kdz_sub_dict
        """

        # Read a whole DZ header
        buf = self.infile.read(self.kdz_sub_len)

        # "Make the item"
        # Create a new dict using the keys from the format string
        # and the format string itself
        # and apply the format to the buffer
        kdz_item = dict(
            zip(
              self.kdz_sub_dict.keys(),
              unpack(self.kdz_formatstring, buf)
              )
        )

        # Collapse (truncate) each key's value if it's listed as collapsible
        # 注意：Python 3 中字节串的 strip 需要传入 bytes 类型
        for key in self.kdz_collapsibles:
            if key[1] == True:
                # 如果值是 bytes 类型，则进行 strip
                if isinstance(kdz_item[key[0]], bytes):
                    kdz_item[key[0]] = kdz_item[key[0]].strip(b'\x00')

        return kdz_item

    def getPartitions(self):
        """
        Returns the list of partitions from a KDZ file containing multiple segments
        """
        while True:
            # Read the current KDZ header
            kdz_sub = self.readKDZHeader()

            # Add it to our list
            self.partitions.append(kdz_sub)

            # Is there another KDZ header?
            # 注意：读取的也是 bytes，比较时也要用 bytes
            if self.infile.read(4) == b'\x00\x00\x00\x00':
                break

            # Rewind file pointer 4 bytes
            # 注意：Python 3 中 os.SEEK_CUR 对应的数字是 1，但推荐使用常量或直接写 1
            self.infile.seek(-4, 1)

        # Make partition list
        # 注意：name 可能是 bytes，尝试解码为字符串以便显示
        part_list = []
        for x in self.partitions:
            name = x['name']
            if isinstance(name, bytes):
                # 尝试以 latin-1 或 utf-8 解码，ignore 错误
                name = name.decode('latin-1', errors='ignore').rstrip('\x00')
            part_list.append((name, x['length']))
        return part_list

    def extractPartition(self, index):
        """
        Extracts a partition from a KDZ file
        """

        currentPartition = self.partitions[index]

        # Seek to the beginning of the compressed data in the specified partition
        self.infile.seek(currentPartition['offset'])

        # Ensure that the output directory exists
        if not os.path.exists(self.outdir):
            os.makedirs(self.outdir)

        # 获取文件名
        name = currentPartition['name']
        if isinstance(name, bytes):
            name = name.decode('latin-1', errors='ignore').rstrip('\x00')

        # Open the new file for writing (binary mode)
        outfile_path = os.path.join(self.outdir, name)
        with open(outfile_path, 'wb') as outfile:
            # Use 1024 byte chunks
            chunkSize = 1024

            while True:
                data = self.infile.read(chunkSize)
                if not data:
                    break
                outfile.write(data)

                # If the output file + chunkSize would exceed the input data length
                if outfile.tell() >= currentPartition['length']:
                    break

        print(f"[+] Extracted: {name}")

    def parseArgs(self):
        # Parse arguments
        parser = argparse.ArgumentParser(description='LG KDZ File Extractor by IOMonster')
        parser.add_argument('-f', '--file', help='KDZ File to read', action='store', required=True, dest='kdzfile')
        group = parser.add_mutually_exclusive_group(required=True)
        group.add_argument('-l', '--list', help='List partitions', action='store_true', dest='listOnly')
        group.add_argument('-x', '--extract', help='Extract all partitions', action='store_true', dest='extractAll')
        group.add_argument('-s', '--single', help='Single Extract by ID', action='store', dest='extractID', type=int)
        parser.add_argument('-o', '--out', help='Output directory', action='store', dest='outdir')

        return parser.parse_args()

    def openFile(self, kdzfile):
        # Open the file in binary mode
        self.infile = open(kdzfile, "rb")

        # Get length of whole file
        self.infile.seek(0, os.SEEK_END)
        self.kdz_length = self.infile.tell()
        self.infile.seek(0, os.SEEK_SET) # 显式指定从开头开始

        # Verify KDZ header
        verify_header = self.infile.read(8)
        if verify_header != self.kdz_header:
            # 注意：Python 3 中 bytes 的 ord() 处理方式
            expected_hex = ' '.join(f'0x{b:02x}' for b in self.kdz_header)
            received_hex = ' '.join(f'0x{b:02x}' for b in verify_header)
            print(f"[!] Error: Unsupported KDZ file format.")
            print(f"[ ] Expected: {expected_hex},")
            print(f"[ ] but received: {received_hex}.")
            sys.exit(0)

    def cmdExtractSingle(self, partID):
        # 边界检查
        if partID < 0 or partID >= len(self.partList):
            print(f"[-] Error: Partition ID {partID} out of range.")
            return
        print("[+] Extracting single partition!\n")
        print(f"[+] Extracting {self.partList[partID][0]} to {os.path.join(self.outdir, self.partList[partID][0])}")
        self.extractPartition(partID)

    def cmdExtractAll(self):
        print("[+] Extracting all partitions!\n")
        for i in range(len(self.partList)):
            self.extractPartition(i)

    def cmdListPartitions(self):
        print("[+] KDZ Partition List\n=========================================")
        for part in enumerate(self.partList):
            print(f"{part[0]:2d} : {part[1][0]} ({part[1][1]} bytes)")

    def main(self):
        args = self.parseArgs()
        
        # 设置输出目录
        if args.outdir:
            self.outdir = args.outdir

        self.openFile(args.kdzfile)
        self.partList = self.getPartitions()

        if args.listOnly:
            self.cmdListPartitions()
        elif args.extractID is not None: # 注意：store 参数如果没有输入不会是 -1，而是 None
            self.cmdExtractSingle(args.extractID)
        elif args.extractAll:
            self.cmdExtractAll()

        # 关闭文件
        if self.infile:
            self.infile.close()

if __name__ == "__main__":
    kdztools = KDZFileTools()
    kdztools.main()

