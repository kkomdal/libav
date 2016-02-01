/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "avscale.h"

unsigned int avscale_version(void)
{
    return LIBAVUTIL_VERSION_INT;
}

const char *avscale_configuration(void)
{
    return LIBAV_CONFIGURATION;
}

const char *avscale_license(void)
{
#define LICENSE_PREFIX "libavscale license: "
    return LICENSE_PREFIX LIBAV_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}