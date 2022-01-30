#ifndef LENSOR_OS_FAT_DRIVER_H
#define LENSOR_OS_FAT_DRIVER_H

#include "integers.h"
#include "FAT_definitions.h"

namespace AHCI {
	class AHCIDriver;
}
class Inode;

/// The FAT driver wraps around the AHCI driver and parse data into/out of VFS format (inode).
class FATDriver {
public:
	FATDriver() {}
	
	void read_to_inode           (AHCI::AHCIDriver* ahci, u8 portNumber, Inode* inode);
	void write_from_inode        (AHCI::AHCIDriver* ahci, u8 portNumber, Inode* inode);
	bool is_device_fat_formatted (AHCI::AHCIDriver* achi, u8 portNumber);

	inline u32 get_total_sectors           (BootRecord* BR);
	inline u32 get_total_fat_sectors       (BootRecord* BR);
	inline u32 get_root_directory_sectors  (BootRecord* BR);
	inline u32 get_total_data_sectors      (BootRecord* BR);
	inline u32 get_first_data_sector       (BootRecord* BR);
	inline u32 get_total_clusters          (BootRecord* BR);
	inline u32 get_first_sector_in_cluster (BootRecord* BR, u32 cluster_number);
	
	FATType get_type (BootRecord* br);
};
extern FATDriver gFATDriver;

#endif