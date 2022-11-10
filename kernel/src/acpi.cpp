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

#include <format>

#include <acpi.h>

#include <cstr.h>
#include <debug.h>
#include <integers.h>

/* Helpful resource: https://github.com/freebsd/freebsd-src/blob/main/usr.sbin/acpi/acpidump/acpi.c */

// Uncomment the following directive for extra debug information output.
//#define DEBUG_ACPI

#ifdef DEBUG_ACPI
#   define DBGMSG(...) std::print(__VA_ARGS__)
#else
#   define DBGMSG(...)
#endif

namespace ACPI {
    void initialize(RSDP2* rootSystemDescriptorPointer) {
        DBGMSG("[ACPI]: Initializing ACPI\r\n");
        if (rootSystemDescriptorPointer == nullptr) {
            std::print("[ACPI]: \033[31mERROR\033[0m -> "
                       "Root System Descriptor Pointer is null. "
                       "(error in bootloader or during boot process)\r\n"
                       );
            return;
        }
        gRSDP = (ACPI::SDTHeader*)rootSystemDescriptorPointer;
        // eXtended System Descriptor Table
        gXSDT = (ACPI::SDTHeader*)(rootSystemDescriptorPointer->XSDTAddress);
        DBGMSG("  RSDP {}\r\n"
               "  XSDT: {}\r\n"
               "[ACPI]: \033[32mInitialized\033[0m\r\n"
               "\r\n"
               , (void*) gRSDP
               , (void*) gXSDT
               );
    }

    SDTHeader* gRSDP { nullptr };
    SDTHeader* gXSDT { nullptr };

    u8 checksum(void* pointer, u64 length) {
        u8* base = (u8*)pointer;
        u8 sum = 0;
        for (; length > 0; length--, base++)
            sum += *base;

        return sum;
    }

    void print_sdt(SDTHeader* header) {
        if (header == nullptr)
            return;

        std::print("Signature: {}\r\n"
                   "  Length: {}\r\n"
                   "  Revision: {}\r\n"
                   "  Checksum: {}\r\n"
                   "  OEM ID: {}\r\n"
                   "  OEM Table ID: {}\r\n"
                   "  OEM Revision: {}\r\n"
                   "  Creator ID: {}\r\n"
                   "  Creator Revision: {}\r\n"
                   , __s(header->Signature)
                   , header->Length
                   , header->Revision
                   , header->Checksum
                   , __s(header->OEMID)
                   , __s(header->OEMTableID)
                   , header->OEMRevision
                   , __s((u8*)&header->CreatorID)
                   , header->CreatorRevision);
    }

    void* find_table(SDTHeader* header, const char* signature) {
        if (header == nullptr || signature == nullptr)
            return nullptr;

        DBGMSG("[ACPI]: Looking for {} table\r\n", signature);
        u64 entries = (header->Length - sizeof(ACPI::SDTHeader)) / 8;

        DBGMSG("  {}: {} entries\r\n", __s(header->Signature), entries);
        for (u64 t = 0; t < entries; ++t) {
            SDTHeader* sdt = (SDTHeader*)*((u64*)((u64)header + sizeof(SDTHeader)) + t);
#ifdef DEBUG_ACPI
            print_sdt(sdt);
#endif /* DEBUG_ACPI */
            // Find matching signature.
            if (strcmp((char*)sdt->Signature, signature, 4)) {
                if (int rc = checksum(sdt, sdt->Length)) {
                    std::print("[ACPI]: \033[31mERROR::\033[0m Invalid checksum on '{}' table: {}\r\n\r\n",
                        __s(sdt->Signature), rc);
                    return nullptr;
                }
                DBGMSG("\r\n");
                return sdt;
            }
        }
        // Could not find table.
        return nullptr;
    }

    void* find_table(const char* signature) {
        return find_table(gXSDT, signature);
    }
}
