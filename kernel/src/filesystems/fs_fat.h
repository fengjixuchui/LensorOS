#ifndef LENSOR_OS_FILE_SYSTEM_FAT_H
#define LENSOR_OS_FILE_SYSTEM_FAT_H

#include "../integers.h"
#include "../ahci.h"
#include "../basic_renderer.h"

#define FAT_DIRECTORY_SIZE_BYTES 32

// Thanks to Gigasoft of osdev forums for this list
// What makes a FAT filesystem valid:
// - Word at 0x1fe equates to 0xaa55
// - Sector size is power of two between 512-4096 (inclusive)
// - Cluster size of a power of two
// - Media type is 0xf0 or greater or equal to 0xf8
// - FAT size is not zero
// - Number of sectors is not zero
// - Number of root directory entries is (zero if fat32) (not zero if fat12/16)
// - Root cluster is valid (FAT32)
// - File system version is zero (FAT32)
// - NumFATsPresent greater than zero

/// File Allocation Table File System
///   Formats storage media into three sections:
///     - Boot Record
///     - File Allocation Table (namesake)
///     - Directory + Data area (they couldn't name
///         this something cool like the other two?)
namespace FatFS {
	/// BIOSParameterBlock
	///   Initial section of first logical sector on storage media.
	///   Contains information such as number of bytes per sector,
	///     num sectors per cluster, num reserved sectors, etc.
	struct BIOSParameterBlock {
		/// Infinite loop to catch a computer trying to
		///   boot from non-bootable drive: `EB FE 90`.
		u8 JumpCode[3];
		/// OEM Identifier
		u8 OEMID[8];
		u16 NumBytesPerSector;
		u8 NumSectorsPerCluster;
		/// Boot record sectors included in this value.
		u16 NumReservedSectors;
		u8 NumFATsPresent;
		u16 NumEntriesInRoot;
		/// Total sectors in logical volume.
		/// If zero, count is stored in `TotalSectors32`.
		u16 TotalSectors16;
		u8 MediaDescriptorType;
		/// FAT12/FAT16 ONLY.
		u16 NumSectorsPerFAT;
		u16 NumSectorsPerTrack;
		/// Number of heads or sides on the storage media.
		/// NOTE: Whatever program formatted the media may have been incorrect
		///         concerning the physical geometry of the media.
		u16 NumHeadsOrSides;
		/// Number of hidden sectors (the LBA of the beginning of the partition).
		u32 NumHiddenSectors;
		u32 TotalSectors32;
	} __attribute__((packed));

	struct BootRecordExtension16 {
		u8 BIOSDriveNumber;
		u8 Reserved;
		u8 BootSignature;
		u32 VolumeID;
		u8 VolumeLabel[11];
		u8 FatTypeLabel[8];
	} __attribute__((packed));

	struct BootRecordExtension32 {
		u32 NumSectorsPerFAT;
		u16 ExtendFlags;
		u16 FatVersion;
		u32 RootCluster;
		u16 FATInformation;
		/// Location of backup of boot record (in case of bad read/corruption).
		u16 BackupBootRecordSector;
		u8 Reserved0[12];
		u8 DriveNumber;
		u8 Reserved1;
		u8 BootSignature;
		u32 VolumeID;
		u8 VolumeLabel[11];
		u8 FatTypeLabel[8];
	} __attribute__((packed));

	/// Boot Record
	///   Starting at logical sector zero of the partition, occupies one sector.
	///   Contains both data and code mixed together.
	struct BootRecord {
		// See above.
		BIOSParameterBlock BPB;
		// This will be cast to it's specific type once the driver parses
		//   what type of FAT this is (extended 16 or extended 32).
		u8 Extended[54];
	} __attribute__((packed));

	enum FATType {
		INVALID = 0,
		FAT12 = 1,
		FAT16 = 2,
		FAT32 = 3,
		ExFAT = 4
	};

	class FATDevice {
	public:
		AHCI::Port* Port;
		FATType Type {INVALID};
		BootRecord BR;

		FATDevice()  {}
		~FATDevice() {}

		inline u32 get_total_sectors() {
			if (BR.BPB.TotalSectors16 == 0) {
				return BR.BPB.TotalSectors32;
			}
			return BR.BPB.TotalSectors16;
		}

		inline u32 get_total_fat_sectors() {
			if (BR.BPB.NumSectorsPerFAT == 0) {
				return (*(BootRecordExtension32*)&BR.Extended).NumSectorsPerFAT;
			}
			return BR.BPB.NumSectorsPerFAT;
		}

		inline u32 get_first_fat_sector() {
			return BR.BPB.NumReservedSectors;
		}

		u32 get_root_directory_sectors() {
			static u32 sRootDirSectors = 0;
			if (sRootDirSectors == 0) {
			    sRootDirSectors = ((BR.BPB.NumEntriesInRoot * FAT_DIRECTORY_SIZE_BYTES)
								   + (BR.BPB.NumBytesPerSector-1)) / BR.BPB.NumBytesPerSector;
			}
			return sRootDirSectors;
		}

		inline u32 get_first_data_sector() {
			static u32 sFirstDataSector = 0;
			if (sFirstDataSector == 0) {
				sFirstDataSector = BR.BPB.NumReservedSectors
					+ (BR.BPB.NumFATsPresent * get_total_fat_sectors())
					+ get_root_directory_sectors();
			}
			return sFirstDataSector;
		}

		inline u32 get_root_directory_start_sector() {
			if (Type == FATType::FAT12 || Type == FATType::FAT16) {
				return get_first_data_sector() - get_root_directory_sectors();				
			}
			else {
				return (*(BootRecordExtension32*)&BR.Extended).RootCluster;
			}
		}

		u32 get_total_data_sectors() {
			static u32 sTotalDataSectors = 0;
			if (sTotalDataSectors == 0) {
			    sTotalDataSectors = get_total_sectors()
				- (BR.BPB.NumReservedSectors
				   + (BR.BPB.NumFATsPresent * get_total_fat_sectors())
				   + get_root_directory_sectors());
			}
			return sTotalDataSectors;
		}

		u32 get_total_clusters() {
			static u32 sTotalClusters = 0;
			if (sTotalClusters == 0) {
				// This rounds down.
			    sTotalClusters = get_total_data_sectors()
					/ BR.BPB.NumSectorsPerCluster;
			}
			return sTotalClusters;
		}

		inline u32 get_cluster_start_sector(u32 cluster) {
			return ((cluster - 2) * BR.BPB.NumSectorsPerCluster)
				+ get_first_data_sector();
		}

		/// Return total size of all sectors formatted in bytes.
		u64 get_total_size() {
			static u64 sTotalSize = 0;
			if (sTotalSize == 0) {
				sTotalSize = get_total_sectors() * BR.BPB.NumBytesPerSector;
			}
			return sTotalSize;
		}
	};

	void print_fat_boot_record(FATDevice* device) {
		u64 totalSectors     = (u64)device->get_total_sectors();
		u64 totalDataSectors = (u64)device->get_total_data_sectors();
		gRend.putstr("FAT Boot Record: ");
		gRend.crlf();
		gRend.putstr("|\\");
		gRend.crlf();
		gRend.putstr("| Total Size: ");
		gRend.putstr(to_string(device->get_total_size() / 1024 / 1024));
		gRend.putstr("mib");
		gRend.crlf();
		gRend.putstr("| |\\");
		gRend.crlf();
		gRend.putstr("| | Total sectors: ");
		gRend.putstr(to_string(totalSectors));
		gRend.crlf();
		gRend.putstr("| \\");
		gRend.crlf();
		gRend.putstr("|  Sector Size: ");
		gRend.putstr(to_string((u64)device->BR.BPB.NumBytesPerSector));
		gRend.crlf();
		gRend.putstr("|\\");
		gRend.crlf();
		gRend.putstr("| Number of Sectors Per Cluster: ");
		gRend.putstr(to_string((u64)device->BR.BPB.NumSectorsPerCluster));
		gRend.crlf();
		gRend.putstr("|\\");
		gRend.crlf();
		gRend.putstr("| Total Usable Size: ");
		gRend.putstr(to_string(totalDataSectors * device->BR.BPB.NumBytesPerSector
							   / 1024 / 1024));
		gRend.putstr("mib");
		gRend.crlf();
		gRend.putstr("| \\");
		gRend.crlf();
		gRend.putstr("|  Total data sectors: ");
		gRend.putstr(to_string(totalDataSectors));
		gRend.crlf();
	}

	/// The FAT Driver will house all functionality pertaining to actually
	///   reading and writing to/from a FATDevice.
	/// This includes:
	///   - Parsing a port to see if it is an eligible FAT device.
	///   - Reading/Writing a file.
	///   - Reading/Writing a directory.
	class FATDriver {
	public:
		FATDevice devices[32];
		u8 numDevices{0};

	    void read_boot_sector(u8 index) {
			// read boot sector from port into device at index.
			gRend.putstr("[FatFS]: Reading boot sector");
			gRend.crlf();
		    if (devices[index].Port->Read(0, 1, devices[index].Port->buffer)) {
				memcpy((void*)devices[index].Port->buffer, &devices[index].BPB, 720);
				print_fat_boot_record(&devices[index]);
				if (devices[index].BPB.NumFATsPresent > 0) {
					u32 totalClusters = devices[index].get_total_clusters();
					if (totalClusters == 0) {
						devices[index].Type = FATType::ExFAT;
					}
					else if (totalClusters < 4085) {
						devices[index].Type = FATType::FAT12;
					}
					else if (totalClusters < 65525) {
						devices[index].Type = FATType::FAT16;
					}
					else {
						devices[index].Type = FATType::FAT32;
					}
				}
			}
			else {
				gRend.putstr("[FatFS]: Unsuccessful read (is device functioning properly?)");
				gRend.crlf();
			}
		}

		bool is_device_fat(AHCI::Port* port) {
			u8 devIndex = numDevices;
			numDevices++;

			devices[devIndex].Port = port;
			
			// Read boot sector from port into device.
			read_boot_sector(devIndex);
			if (devices[devIndex].Type == FATType::INVALID) {
				numDevices--;
				return false;
			}
			return true;
		}

		void read_root_directory() {
			
		}
	};
}

#endif
