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

#include <system.h>
#include <storage/device_drivers/pipe.h>

auto PipeDriver::open(std::string_view path) -> std::shared_ptr<FileMetadata> {
    PipeBuffer* pipe = nullptr;
    if (FreePipeBuffers.empty()) {
        pipe = new PipeBuffer();
    } else {
        pipe = FreePipeBuffers.back();
        FreePipeBuffers.pop_back();
    }
    return std::make_shared<FileMetadata>(path, sdd(SYSTEM->virtual_filesystem().PipesDriver), PIPE_BUFSZ, pipe);
}