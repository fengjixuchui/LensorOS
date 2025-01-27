/* Copyright 2022, Contributors To LensorOS.
 * All rights reserved.
 *
 * This file is part of LensorOS.
 *
 * LensorOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LensorOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LensorOS. If not, see <https://www.gnu.org/licenses
 */

#include <fat_definitions.h>
#include <format>
#include <integers.h>
#include <memory>
#include <storage/file_metadata.h>
#include <storage/filesystem_drivers/file_allocation_table.h>
#include <string>
#include <vector>

// Uncomment the following directive for extra debug information output.
//#define DEBUG_FAT

#ifdef DEBUG_FAT
#   define DBGMSG(...) std::print(__VA_ARGS__)
#else
#   define DBGMSG(...)
#endif

/// Given "/foo/bar/baz.txt" return "foo" and set path to "bar/baz.txt"
/// Given "/" return "/"
std::string pop_filename_from_front_of_path(std::string& raw_path) {
    /// Strip leading slash.
    std::string path = raw_path;
    if (path.starts_with("/")) path = path.substr(1);
    if (path.size() < 1) return raw_path;

    raw_path = path;
    size_t first_sep = path.find_first_of("/");
    if (first_sep == std::string::npos) return path;

    std::string out = path.substr(0, first_sep);
    raw_path = path.substr(first_sep);
    return out;
}

void FileAllocationTableDriver::print_fat(BootRecord& br) {
    std::print("File Allocation Table Boot Record:\n"
               "  Total Clusters:      {}\n"
               "  Sectors / Cluster:   {}\n"
               "  Total Sectors:       {}\n"
               "  Bytes / Sector:      {}\n"
               "  Sectors / FAT:       {}\n"
               "  Sector Offsets:\n"
               "    FATs:      {}\n"
               "    Data:      {}\n"
               "    Root Dir.: {}\n"
               "\n"
               , br.total_clusters()
               , br.BPB.NumSectorsPerCluster
               , br.BPB.total_sectors()
               , u16(br.BPB.NumBytesPerSector)
               , br.fat_sectors()
               , br.BPB.first_fat_sector()
               , br.first_data_sector()
               , br.first_root_directory_sector()
               );
}

FATType FileAllocationTableDriver::fat_type(BootRecord& br) {
    u64 totalClusters = br.total_clusters();
    if (totalClusters == 0) return FATType::ExFAT;
    else if (totalClusters < 4085) return FATType::FAT12;
    else if (totalClusters < 65525) return FATType::FAT16;
    else return FATType::FAT32;
}

auto FileAllocationTableDriver::try_create(std::shared_ptr<StorageDeviceDriver> driver) -> std::shared_ptr<FilesystemDriver> {
    if (!driver) return nullptr;

    BootRecord br;
    auto n_read = driver->read_raw(0, sizeof br, &br);
    if (n_read != sizeof br) {
        std::print("Failed to read boot record from device\n");
        return nullptr;
    }

    u64 totalSectors = br.BPB.TotalSectors16 == 0
                       ? br.BPB.TotalSectors32
                       : br.BPB.TotalSectors16;

    /* Validate boot sector is of FAT format.
     * TODO: Use more of these confidence checks before
     *       assuming it is valid FAT filesystem.
     * What makes a FAT filesystem valid?
     * Thanks to Gigasoft of osdev forums for this list.
     * [x] = something this driver checks.
     * [x] Word at byte offset 510 equates to 0xaa55.
     * [x] Sector size is power of two between 512-4096 (inclusive).
     * [x] Cluster size is a power of two.
     * [ ] Media type is 0xf0 or greater or equal to 0xf8.
     * [ ] FAT size is not zero.
     * [x] Number of sectors is not zero.
     * [ ] Number of root directory entries is:
     *       - zero if FAT32.
     *       - not zero if FAT12 or FAT16.
     * [ ] (FAT32) Root cluster is valid
     * [ ] (FAT32) File system version is zero
     * [x] NumFATsPresent greater than zero
     */
    bool out = (br.Magic == 0xaa55
                && totalSectors != 0
                && br.BPB.NumBytesPerSector >= 512
                && br.BPB.NumBytesPerSector <= 4096
                && (br.BPB.NumBytesPerSector
                    & (br.BPB.NumBytesPerSector - 1)) == 0
                && (br.BPB.NumSectorsPerCluster
                    & (br.BPB.NumSectorsPerCluster - 1)) == 0
                && br.BPB.NumFATsPresent > 0);

    if (!out) return nullptr;

#ifdef DEBUG_FAT
    print_fat(br);
#endif /* DEBUG_FAT */

    auto fs = std::make_shared<FileAllocationTableDriver>(std::move(driver), std::move(br));
    fs->This = fs;
    return std::static_pointer_cast<FilesystemDriver>(fs);
}

// "abcdefgh.ijk" needs to become "ABCDEFGHIJK"
// "ABCDEFGHIJK" needs to become "ABCDEFGHIJK"
// "blazeit" needs to become "BLAZEIT    "
auto FileAllocationTableDriver::translate_filename(std::string_view raw_filename) -> std::string {
    std::string path = raw_filename;

    // toupper
    for (usz i = 0; i < path.size(); ++i)
        if (path[i] >= 97 && path[i] <= 122) path[i] -= 32;

    // Check if filename is in valid 8.3 format already
    if (path.size() == 12 && path[8] == '.') {
        // Erase period from name (i.e. "ABCDEFGH.IJK" -> "ABCDEFGHIJK")
        path.erase(8, 1);

        DBGMSG("[FAT]: Got perfect 8.3 \"{}\"\n", path);

        return path;
    } else if (path.size() <= 12) {
        // "blazeit" -> "BLAZEIT    "
        // "foo.a"   -> "FOO     A  "

        // TODO: What about multiple '.' in filename?
        // TODO: What about over-long extension? How does that interact
        // with LFN?

        std::string name;
        std::string extension;

        // Find last '.'.
        size_t last_dot = path.find_last_of(".");
        if (last_dot != std::string::npos) {
            // If last '.' is past eighth byte, then there is no way
            // the filename can fit in the eight bytes allotted to it.
            // Need to return computer-generated filename or the LFN,
            // or something.
            if (last_dot > 8) {
                std::print("[FAT]: TODO: translate_filename() computer-generated 8.3 filenames... (path short, name long)\n");
                return "INVALID_TRANSLATION";
            }

            // If it exists, the three bytes following it are the
            // extension.

            for (size_t i = last_dot + 1; i < path.size(); ++i)
                extension += path[i];

            // If less than three bytes follow the '.', then spaces are
            // appended until three bytes are reached.
            for (size_t i = 0; i < 3; ++i)
                extension += ' ';

            // Truncate to three bytes (over-long extension).
            extension = extension.substr(0, 3);

            name = path.substr(0, 8);

            DBGMSG("[FAT]: Got name \"{}\" and extension \"{}\"\n", name, extension);

            return name + extension;

        } else {
            // If no '.' in filename, ensure it's length is less than or equal to 8 bytes.

            // If it is longer than eight bytes, make computer-generated filename...
            if (path.size() > 11) {
                std::print("[FAT]: TODO: translate_filename() computer-generated 8.3 filenames... (path short, name long, no extension)\n");
                return "INVALID_TRANSLATION";
            }

            // Pad filename with spaces to reach full 11-byte 8.3 filename length.
            for (usz i = path.size(); i < 11; ++i)
                path += ' ';

            DBGMSG("[FAT]: Got filename \"{}\" (no extension)\n", path);

            return path;
        }
    } else {
        // Name is too long, have to do computer-generated short file
        // name, or look for long file name entry... it really depends
        // on where this is called from.
        std::print("[FAT]: TODO: translate_filename() computer-generated 8.3 filenames... (path long)\n");

        return "INVALID_TRANSLATION";
    }
    // UNREACHABLE();
}

FileAllocationTableDriver::DirIteratorHelper::Iterator::Iterator(FileAllocationTableDriver& driver, u32 directoryCluster)
: Driver(driver), ClusterIndex(directoryCluster) {
    /// Read first entry. This MUST initialise MoreClusters to false
    /// if there are are no entries at all. In other words, when this
    /// function returns, either MoreClusters is false or Entry contains
    /// a valid entry.
    ReadNextCluster();
    ++*this;
}

void FileAllocationTableDriver::DirIteratorHelper::Iterator::ReadNextCluster() {
    const u64 clusterSector = Driver.BR.cluster_to_sector(ClusterIndex);
    Driver.Device->read_raw(clusterSector * Driver.BR.BPB.NumBytesPerSector, ClusterSize, ClusterContents.data());
    Entry.CE = reinterpret_cast<ClusterEntry*>(ClusterContents.data());
    Entry.LongFileName.clear();
    ClearLFN = false;
}

void FileAllocationTableDriver::DirIteratorHelper::Iterator::TryReadNextCluster() {
    // Check if this is the last cluster in the chain.
    const u64 clusterNumber = Entry.CE->get_cluster_number();
    u64 FAToffset = 0;
    switch (Driver.Type) {
        case FATType::FAT12: FAToffset = clusterNumber + (clusterNumber / 2); break;
        case FATType::FAT16: FAToffset = clusterNumber * 2; break;
        case FATType::ExFAT:
        case FATType::FAT32:
        default:
            FAToffset = clusterNumber * 4;
            break;
    }

    const u64 FATsector = Driver.BR.BPB.first_fat_sector() + (FAToffset / Driver.BR.BPB.NumBytesPerSector);
    const u64 entryOffset = FAToffset % Driver.BR.BPB.NumBytesPerSector;
    if (entryOffset <= 1 || FATsector == LastFATSector) {
        MoreClusters = false;
        return;
    }

    std::vector<u8> FAT(Driver.BR.BPB.NumBytesPerSector);
    LastFATSector = FATsector;
    Driver.Device->read_raw(FATsector * Driver.BR.BPB.NumBytesPerSector, Driver.BR.fat_sectors() * Driver.BR.BPB.NumBytesPerSector, FAT.data());
    u64 tableValue = 0;
    switch (Driver.Type) {
        case FATType::FAT12:
            tableValue = *(reinterpret_cast<u16*>(&FAT[entryOffset]));
            if (clusterNumber & 0b1) tableValue >>= 4;
            else tableValue &= 0x0fff;
            if (tableValue >= 0x0ff8) MoreClusters = false;
            // TODO: Hande tableValue == 0x0ff7 (bad cluster)
            break;
        case FATType::FAT16:
            tableValue = *(reinterpret_cast<u16*>(&FAT[entryOffset]));
            tableValue &= 0xffff;
            if (tableValue >= 0xfff8) MoreClusters = false;
            // TODO: Hande tableValue == 0xfff7 (bad cluster)
            break;
        case FATType::FAT32:
            tableValue = *(reinterpret_cast<u32*>(&FAT[entryOffset]));
            tableValue &= 0xfffffff;
            if (tableValue >= 0x0ffffff8) MoreClusters = false;
            // TODO: Hande tableValue == 0x0ffffff7 (bad cluster)
            break;
        case FATType::ExFAT:
            tableValue = *(reinterpret_cast<u32*>(&FAT[entryOffset]));
            break;
        default:
            break;
    }

    ClusterIndex = tableValue;
    if (MoreClusters) ReadNextCluster();
}

auto FileAllocationTableDriver::DirIteratorHelper::Iterator::operator++() -> Iterator& {
    // TODO: ExFAT will need it's own code flow, essentially.
    Entry.CE++;
    while (MoreClusters) {
        while (Entry.CE->FileName[0] != 0) {
            if (Entry.CE->FileName[0] == 0xe5)
                continue;

            if (ClearLFN) {
                Entry.LongFileName.clear();
                ClearLFN = false;
            }

            if (Entry.CE->long_file_name()) {
                auto* lfn = reinterpret_cast<LFNClusterEntry*>(Entry.CE);
                Entry.LongFileName += std::string((const char*) &lfn->Characters1[0], sizeof(u16) * 5);
                Entry.LongFileName += std::string((const char*) &lfn->Characters2[0], sizeof(u16) * 6);
                Entry.LongFileName += std::string((const char*) &lfn->Characters3[0], sizeof(u16) * 2);
                Entry.CE++;
                continue;
            }

            // Remove 0xff and then two 0x00 from end of longFileName.
            ClearLFN = true;
            Entry.FileName = std::string_view{reinterpret_cast<char*>(Entry.CE->FileName), 11};
            Entry.LongFileName.__remove_trailing("\xff\0");

#ifdef DEBUG_FAT
            std::string fileType;

            if (Entry.CE->read_only()) fileType += "read-only ";
            if (Entry.CE->hidden()) fileType += "hidden ";
            if (Entry.CE->system()) fileType += "system ";
            if (Entry.CE->archive()) fileType += "archive ";

            if (Entry.CE->directory()) fileType += "directory ";
            else if (Entry.CE->volume_id()) fileType += "volume identifier ";
            else fileType += "file ";

            std::print("    Found {}named \"{}\" (\"{}\")\n", fileType, Entry.FileName, Entry.LongFileName);
#endif

            Entry.ByteOffset = Driver.BR.cluster_to_sector(Entry.CE->get_cluster_number()) * Driver.BR.BPB.NumBytesPerSector;
            return *this;
        }

        TryReadNextCluster();
    }

    return *this;
}

std::shared_ptr<FileMetadata> FileAllocationTableDriver::traverse_path(std::string_view raw_path, u32 directoryCluster) {
    // If directoryCluster == -1, replace it with the root directory.
    if (directoryCluster == u32(-1))
        directoryCluster = BR.sector_to_cluster(BR.first_root_directory_sector());

    /// Strip leading slash.
    if (raw_path.starts_with("/")) raw_path = raw_path.substr(1);
    if (raw_path.size() < 1) {
        DBGMSG("[FAT]:open(): Invalid path: {}\n", raw_path);
        return {};
    }

    // Get first filename from path.
    // Given path "foo/bar/bas.exe", return "foo" as a legal FAT
    // filename, and alter given path to be "past" that + directory
    // separator.
    std::string path = raw_path;
    auto raw_filename = pop_filename_from_front_of_path(path);
    DBGMSG("[FAT]:open(): Got filename \"{}\" and path \"{}\" from \"{}\"\n", raw_filename, path, raw_path);

    // Translate path (FAT has very limited file names).
    std::string filename = translate_filename(raw_filename);
    DBGMSG("[FAT]:open(): Translated filename \"{}\" from \"{}\"\n", filename, raw_filename);

    for (const auto& Entry : for_each_dir_entry_in(directoryCluster)) {
        // If path and raw_filename are equal, we can not resolve any more
        // filenames from full path; we have found the file.
        if (Entry.FileName != filename && (Entry.LongFileName.empty() || Entry.LongFileName != filename)) continue;
        if (path == raw_filename) {
            DBGMSG("  Found file at {}!\n"
                   "    Name: \"{}\"\n"
                   "    Long: \"{}\"\n"
                   , path
                   , Entry.FileName
                   , Entry.LongFileName
                   );
            // TODO: directory vs. file metadata
            return std::make_shared<FileMetadata>
                       (std::move(filename),
                        sdd(This.lock()),
                        u32(Entry.CE->FileSizeInBytes),
                        (void*) Entry.ByteOffset
                        );
        }

        // Otherwise, we need to recurse into the directory.
        if (!Entry.CE->directory()) {
            std::print(R"([FAT]: Cannot follow path "{}" because "{}" is not a directory)", path, filename);
            return {};
        }

        // Recurse into directory...
        u32 dirCluster = Entry.CE->get_cluster_number();
        //std::print("Recursing! Following {} at cluster {}\n", path, dirCluster);
        return traverse_path(path, dirCluster);
    }

    /// No such file.
    std::print("[FAT]: Could not find file at \"{}\", sorry\n", filename);
    return {};
}

auto FileAllocationTableDriver::open(std::string_view raw_path) -> std::shared_ptr<FileMetadata> {
    DBGMSG("[FAT]: Attempting to open file {}\n", raw_path);
    if (Device == nullptr) {
        /// Should never get here.
        std::print("FileAllocationTableDriver::open(): Device is null!\n");
        return {};
    }

#ifdef DEBUG_FAT
    auto __this = This.lock();
    if (!__this) {
        /// Should never get here.
        std::print("[FAT]::open(): `This` is null!\n");
        return {};
    }
#endif

    return traverse_path(raw_path);
}
