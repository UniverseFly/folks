// fs.cpp: File System

#include "sfs/fs.h"
#include "sfs/disk.h"

#include <algorithm>

#include <assert.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// Debug file system -----------------------------------------------------------

void FileSystem::debug(Disk *disk) {
  Block block;

  // Read Superblock
  disk->read(0, block.Data);

  printf("SuperBlock:\n");
  printf("    magic number is %s\n", block.Super.MagicNumber == MAGIC_NUMBER ? "valid" : "invalid");
  printf("    %u blocks\n"         , block.Super.Blocks);
  printf("    %u inode blocks\n"   , block.Super.InodeBlocks);
  printf("    %u inodes\n"         , block.Super.Inodes);

  // The total number of Inode blocks
  const uint32_t inodeBlocks = block.Super.InodeBlocks;
  const uint32_t inodeCount = block.Super.Inodes;
  for (uint32_t i = 0; i != inodeBlocks; ++i) {
    // +1 cuz inode blocks start from 1
    disk->read(i + 1, block.Data);
    for (uint32_t inodeIndex = 0; inodeIndex != INODES_PER_BLOCK; ++inodeIndex) {
      // overall index over all inodes
      const auto inodeOverallIndex = i * INODES_PER_BLOCK + inodeIndex;
      // There wouldn't be any valid inodes
      if (i == inodeBlocks - 1 && inodeOverallIndex >= inodeCount) {
        break;
      }
      const auto &inode = block.Inodes[inodeIndex];
      if (inode.Valid == 1) {
        printf("Inode %u:\n", inodeOverallIndex);
        printf("    size: %u bytes\n", inode.Size);
        // The total number of blocks related to this inode
        // x + y - 1 / y == ceil(x/y)
        const uint32_t totalBlocks = (inode.Size + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
        // Here we only calculate the direct blocks. 5 here cuz for an inode block 5 ptrs are direct.
        if (totalBlocks <= 5) {
          // only direct blocks
          printf("    direct blocks:");
          for (uint32_t k = 0; k != totalBlocks; ++k) {
            printf(" %u", inode.Direct[k]);   
          }
          printf("\n");
        } else {
          // first print 5 direct blocks
          printf("    direct blocks: %u %u %u %u %u\n",
                 inode.Direct[0], inode.Direct[1], inode.Direct[2],
                 inode.Direct[3], inode.Direct[4]);
          // then print the indirect block
          printf("    indirect block: %u\n", inode.Indirect);
          // finally print all indirect blocks in this indirect block
          // k stands for the indirect block index, starting from 5
          // k + 5 != ... instead of k != ... - 5 cuz they're unsigned
          Block indirectBlock;
          disk->read(inode.Indirect, indirectBlock.Data);
          printf("    indirect data blocks:");
          for (uint32_t k = 0; k + 5 != totalBlocks; ++k) {
            printf(" %u", indirectBlock.Pointers[k]);
          }
          printf("\n");
        }
      }
    }
  }
}

// Format file system ----------------------------------------------------------

bool FileSystem::format(Disk *disk) {
  if (disk->mounted()) { return false; }
  // Write superblock
  Block superblock;
  superblock.Super.MagicNumber = MAGIC_NUMBER;
  superblock.Super.Blocks = disk->size();
  // ceiling
  superblock.Super.InodeBlocks = (disk->size() + 10 - 1) / 10;
  superblock.Super.Inodes = superblock.Super.InodeBlocks * INODES_PER_BLOCK;
  disk->write(0, superblock.Data);

  // Clear all other blocks
  Block emptyBlock;
  memset(&emptyBlock.Data, 0, sizeof(emptyBlock));
  // note the i+1 here, otherwise the index will exceed the array boundary.
  for (uint32_t i = 0; i + 1 < disk->size(); ++i) {
    disk->write(i + 1, emptyBlock.Data);
  }
  return true;
}

// Mount file system -----------------------------------------------------------

bool FileSystem::mount(Disk *disk) {
  if (disk->mounted()) { return false; }
  // Read superblock
  const auto superblock = getSuperblock(disk);
  if (superblock.MagicNumber != MAGIC_NUMBER) {
    return false;
  }

  // if # of blocks is zero, it must be wrong
  if (superblock.Blocks == 0) {
    return false;
  }
  
  // # of inodes and # of superblock.inodes should be consistent
  if (superblock.Inodes != superblock.InodeBlocks * INODES_PER_BLOCK) {
    return false;
  }

  // # of blocks must be > # of InodeBlocks
  if (superblock.Blocks < superblock.InodeBlocks) {
    return false;
  }

  // Set device and mount
  disk->mount();

  // Copy metadata
  this->disk = disk;
  
  // Allocate free block bitmap
  freeBlocks = std::vector<bool>(disk->size(), true);
  freeBlocks[0] = false;
  Block inodeBlock;
  for (uint32_t i = 0; i < superblock.InodeBlocks; ++i) {
    disk->read(i + 1, inodeBlock.Data);
    freeBlocks[i + 1] = false;
    initFreeBlocks_forInodeBlock(inodeBlock.Inodes);
  }

  return true;
}

void FileSystem::initFreeBlocks_forInodeBlock(const Inode (&inodes)[INODES_PER_BLOCK]) {
  const auto disk = getDisk();
  for (uint32_t i = 0; i < INODES_PER_BLOCK; ++i) {
    const auto &inode = inodes[i];
    if (inode.Valid == 1) {
      // The total number of blocks related to this inode
      // x + y - 1 / y == ceil(x/y)
      const uint32_t totalBlocks = (inode.Size + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
      // Here we only calculate the direct blocks. 5 here cuz for an inode block 5 ptrs are direct.
      if (totalBlocks == 0) {}
      else if (totalBlocks <= 5) {
        // only direct blocks
        for (uint32_t k = 0; k != totalBlocks; ++k) {
          freeBlocks[inode.Direct[k]] = false;
        }
      } else {
        freeBlocks[inode.Direct[0]] = false;
        freeBlocks[inode.Direct[1]] = false;
        freeBlocks[inode.Direct[2]] = false;
        freeBlocks[inode.Direct[3]] = false;
        freeBlocks[inode.Direct[4]] = false;
        freeBlocks[inode.Indirect] = false;

        // k stands for the indirect block index, starting from 5
        // k + 5 != ... instead of k != ... - 5 cuz they're unsigned
        Block indirectBlock;
        disk->read(inode.Indirect, indirectBlock.Data);
        for (uint32_t k = 0; k + 5 != totalBlocks; ++k) {
          freeBlocks[indirectBlock.Pointers[k]] = false;
        }
      }
    }
  }
}

// Create inode ----------------------------------------------------------------

ssize_t FileSystem::create() {
  // Locate free inode in inode table
  const auto disk = getDisk();
  const auto &superblock = getSuperblock();
  // Iterate through inode blocks, and then for each
  // block iterate through all inodes
  Block inodeBlock;
  for (uint32_t i = 0; i < superblock.InodeBlocks; ++i) {
    disk->read(i + 1, inodeBlock.Data);
    for (uint32_t j = 0; j < INODES_PER_BLOCK; ++j) {
      auto &inode = inodeBlock.Inodes[j];
      // Because inodes are all located at the start of the disk,
      // if we can find an invalid one it can be used for creation.
      if (inode.Valid == 0) {
        inode.Valid = 1;
        inode.Size = 0;
        // make inode change persistent
        disk->write(i + 1, inodeBlock.Data);
        // the inumber
        return i * INODES_PER_BLOCK + j;
      }
    }
  }
  
  // Record inode if not found
  return -1;
}

// Remove inode ----------------------------------------------------------------

bool FileSystem::remove(size_t inumber) {
  // Load inode information
  Block inodeBlock;
  auto &inode = getInode(inumber, inodeBlock);
  if (inode.Valid == 0) { return false; }

  // The total number of blocks related to this inode
  // x + y - 1 / y == ceil(x/y)
  const uint32_t totalBlocks = (inode.Size + Disk::BLOCK_SIZE - 1) / Disk::BLOCK_SIZE;
  // Here we only calculate the direct blocks. 5 here cuz for an inode block 5 ptrs are direct.
  if (totalBlocks == 0) {}
  else if (totalBlocks <= 5) {
    // free direct blocks
    for (uint32_t k = 0; k != totalBlocks; ++k) {
      freeBlocks[inode.Direct[k]] = true;
    }
  } else {
    // free indirect blocks
    freeBlocks[inode.Direct[0]] = true;
    freeBlocks[inode.Direct[1]] = true;
    freeBlocks[inode.Direct[2]] = true;
    freeBlocks[inode.Direct[3]] = true;
    freeBlocks[inode.Direct[4]] = true;
    freeBlocks[inode.Indirect] = true;

    // k stands for the indirect block index, starting from 5
    // k + 5 != ... instead of k != ... - 5 cuz they're unsigned
    Block indirectBlock;
    disk->read(inode.Indirect, indirectBlock.Data);
    for (uint32_t k = 0; k + 5 != totalBlocks; ++k) {
      freeBlocks[indirectBlock.Pointers[k]] = true;
    }
  }

  // Clear inode in inode table
  // No need to clean other fields since it's an invalid inode
  inode.Valid = 0;
  disk->write(getInodeBlkIndex(inumber), inodeBlock.Data);

  return true;
}

// Inode stat ------------------------------------------------------------------

ssize_t FileSystem::stat(size_t inumber) {
  // Load inode information
  Block inodeBlock;
  uint32_t inodeBlkIndex = inumber / INODES_PER_BLOCK + 1;
  uint32_t offset = inumber % INODES_PER_BLOCK;
  
  if (inodeBlkIndex > disk->size()) { return -1; }
  disk->read(inodeBlkIndex, inodeBlock.Data);
  if (inodeBlock.Inodes[offset].Valid == 1) {
    return inodeBlock.Inodes[offset].Size;
  }

  return -1;
}

// Read from inode -------------------------------------------------------------

ssize_t FileSystem::read(size_t inumber, char *data, size_t length, size_t offset) {
  // Load inode information
  Block inodeBlock;

  auto &inode = getInode(inumber, inodeBlock);
  if (inode.Valid == 0) {
    return -1;
  }
  
  // Adjust length
  if (offset >= inode.Size) {
    return -1;
  }

  length = length > inode.Size - offset ? inode.Size - offset : length;

  // Read block and copy to data
  uint32_t startBlk = offset / Disk::BLOCK_SIZE;

  // the offset point to read from the first block
  uint32_t fstBlkStartOffset = offset % Disk::BLOCK_SIZE;
  
  Block buffer;
  Block indirectBlk;
  auto blkIndex = startBlk;
  uint32_t writeCount = 0;
  while (writeCount < length) {
    auto diskBlkNo = getDiskBlkNo(inode, blkIndex, indirectBlk);
    disk->read(diskBlkNo, buffer.Data);
    for (uint32_t j = blkIndex == startBlk ? fstBlkStartOffset : 0;
         j < Disk::BLOCK_SIZE and writeCount < length;
         ++j) {
      data[writeCount++] = buffer.Data[j];
    }
  }
  
  return length;
}

// Write to inode --------------------------------------------------------------

ssize_t FileSystem::write(size_t inumber, char *data, size_t length, size_t offset) {
  // Load inode
  Block inodeBlock;

  auto &inode = getInode(inumber, inodeBlock);
  if (inode.Valid == 0) {
    return -1;
  }
  
  if (offset > inode.Size) {
    return -1;
  }

  uint32_t startBlk = offset / Disk::BLOCK_SIZE;

  // write the first block starting at this offset.
  uint32_t fstBlkStartOffset = offset % Disk::BLOCK_SIZE;
  
  Block indirectBlk;
  Block dataBlock;
  uint32_t writeCount = 0;
  uint32_t blkIndex = startBlk;
  while (writeCount < length) {
    uint32_t blk; // data block No.
    // need to allocate a new block for inode
    if (blkIndex >= blockCount(inode)) {
      blk = allocateBlockForInode(inode);
      if (blk == -1) {
        if (offset + writeCount > inode.Size){
          inode.Size = offset + writeCount;
        }
        disk->write(getInodeBlkIndex(inumber), inodeBlock.Data);
        return writeCount;
      }
    } else {
      blk = getDiskBlkNo(inode, blkIndex, indirectBlk);
    }
    disk->read(blk, dataBlock.Data);
    // write to data block
    for (uint32_t i = blkIndex == startBlk ? fstBlkStartOffset : 0;
         i < Disk::BLOCK_SIZE and writeCount < length;
         ++i) {
      dataBlock.Data[i] = data[writeCount++];
    }
    
    disk->write(blk, dataBlock.Data);
    if (offset + writeCount > inode.Size){
      inode.Size = offset + writeCount;
    }
    blkIndex += 1;
  }
  disk->write(getInodeBlkIndex(inumber), inodeBlock.Data);
  return writeCount;
}

ssize_t FileSystem::allocateBlockForInode(Inode &inode) {
  uint32_t blocks = blockCount(inode);
  // allocate a direct block
  if (blocks < POINTERS_PER_INODE) {
    auto blk = allocateBlock();
    // failed to allocate
    if (blk == -1) {
      return -1;
    }
    // `blocks` is the next index
    inode.Direct[blocks] = blk;
    return blk;
  }
  // need to alloc an indirect block besides a data block
  else if (blocks == POINTERS_PER_INODE) {
    auto indBlk = allocateBlock();
    if (indBlk == -1) {
      return -1;
    }
    
    auto blk = allocateBlock();
    if (blk == -1) {
      // reclaim the indirect blk
      reclaimBlock(indBlk);
      return -1;
    }
    inode.Indirect = indBlk;
    Block ptrBlock;
    ptrBlock.Pointers[0] = blk;
    disk->write(indBlk, ptrBlock.Data);
    return blk;
  }
  // indirect data block
  else {
    auto blk = allocateBlock();
    if (blk == -1) {
      return -1;
    }
    Block ptrBlock;
    disk->read(inode.Indirect, ptrBlock.Data);
    ptrBlock.Pointers[blocks - POINTERS_PER_INODE] = blk;
    disk->write(inode.Indirect, ptrBlock.Data);
    return blk;
  }
  // not reachable
  throw std::runtime_error("Not reachable");
  return -1;
}
