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

#ifndef LENSOR_OS_GPT_PARTITION_DRIVER_H
#define LENSOR_OS_GPT_PARTITION_DRIVER_H

#include <integers.h>
#include <guid.h>
#include <storage/storage_device_driver.h>

class GPTPartitionDriver final : public StorageDeviceDriver {
public:
    GPTPartitionDriver(StorageDeviceDriver* driver
                       , GUID type, GUID unique
                       , u64 startSector, u64 sectorSize)
        : Driver(driver)
        , Type(type), Unique(unique)
        , Offset(startSector * sectorSize) {}

    void read(u64 byteOffset, u64 byteCount, u8* buffer) final {
        Driver->read(byteOffset + Offset, byteCount, buffer);
    };

    void write(u64 byteOffset, u64 byteCount, u8* buffer) final {
        Driver->read(byteOffset + Offset, byteCount, buffer);
    };

    GUID type_guid() { return Type; }
    GUID unique_guid() { return Unique; }

private:
    StorageDeviceDriver* Driver { nullptr };
    GUID Type;
    GUID Unique;
    /// Number of bytes to offset within storage device for start of partition.
    u64 Offset { 0 };
};

#endif /* LENSOR_OS_GPT_PARTITION_DRIVER_H */
