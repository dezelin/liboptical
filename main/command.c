/*
    command.c - Multi-Media Commands - 5 (MMC-5).
    Copyright (C) 2006  Aleksandar Dezelin <dezelin@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdafx.h>

#include "adapter.h"
#include "command.h"
#include "errors.h"
#include "feature.h"
#include "helpers.h"
#include "parsers.h"
#include "sysdevice.h"
#include "types.h"

#include <assert.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>

/*
 * MMC opcodes
 */

#define MMC_OPCODE_BLANK	0x00a1
#define MMC_OPCODE_INQUIRY	0x0012
#define MMC_OPCODE_GET_CONFIG	0x0046

/*
 * Constants used throughout the code
 */

#define MAX_GET_CONFIG_TRANSFER_LEN	65530

/*
 * Helper functions
 */

/*
 * Command functions
 */

RESULT optcl_command_blank(const optcl_device *device,
			   const optcl_mmc_blank *command)
{
	uint8_t cdb[12];

	assert(device != 0);
	assert(command != 0);

	if (device == 0 || command == 0) {
		return(E_INVALIDARG);
	}

	/*
	 * Execute command
	 */

	memset(cdb, 0, sizeof(cdb));

	cdb[0] = MMC_OPCODE_BLANK;
	cdb[1] = (command->immed << 4) || command->blanking_type;
	cdb[2] = (uint8_t)(uint32_to_le(command->start_address) >> 24);
	cdb[3] = (uint8_t)((uint32_to_le(command->start_address) << 8) >> 24);
	cdb[4] = (uint8_t)((uint32_to_le(command->start_address) << 16) >> 24);
	cdb[5] = (uint8_t)((uint32_to_le(command->start_address) << 24) >> 24);

	return optcl_device_command_execute(
		device,
		cdb,
		sizeof(cdb),
		0,
		0
		);
}

RESULT optcl_command_get_configuration(const optcl_device *device,
				       const optcl_mmc_get_configuration *command,
				       optcl_mmc_response_get_configuration **response)
{
	RESULT error;
	RESULT destroy_error;
	uint8_t rt;
	uint8_t cdb[10];
	int32_t data_length;
	uint16_t start_feature;
	ptr_t mmc_response = 0;
	uint32_t transfer_size;
	uint32_t alignment_mask;
	uint32_t max_transfer_len;
	optcl_adapter *adapter = 0;
	optcl_list_iterator it = 0;
	optcl_feature_descriptor *descriptor = 0;
	optcl_mmc_response_get_configuration *nresponse0 = 0;
	optcl_mmc_response_get_configuration *nresponse1 = 0;

	assert(device != 0);
	assert(command != 0);
	assert(response != 0);

	if (device == 0 || command == 0 || response == 0) {
		return(E_INVALIDARG);
	}

	assert(
		command->rt == MMC_GET_CONFIG_RT_ALL 
		|| command->rt == MMC_GET_CONFIG_RT_CURRENT 
		|| command->rt == MMC_GET_CONFIG_RT_FROM
		);

	if (command->rt != MMC_GET_CONFIG_RT_ALL 
		&& command->rt != MMC_GET_CONFIG_RT_CURRENT 
		&& command->rt != MMC_GET_CONFIG_RT_FROM)
	{
		return(E_INVALIDARG);
	}

	assert(command->start_feature >= 0);

	if (command->start_feature < 0) {
		return(E_INVALIDARG);
	}

	error = optcl_device_get_adapter(device, &adapter);

	if (FAILED(error)) {
		return(error);
	}

	error = optcl_adapter_get_alignment_mask(adapter, &alignment_mask);

	if (FAILED(error)) {
		destroy_error = optcl_adapter_destroy(adapter);
		return(SUCCEEDED(destroy_error) ? error : destroy_error);
	}

	error = optcl_adapter_get_max_transfer_len(adapter, &max_transfer_len);

	if (FAILED(error)) {
		destroy_error = optcl_adapter_destroy(adapter);
		return(SUCCEEDED(destroy_error) ? error : destroy_error);
	}

	error = optcl_adapter_destroy(adapter);

	if (FAILED(error)) {
		return(error);
	}

	/*
	 * Execute command just to get data length
	 */

	rt = command->rt & 0x03;
	start_feature = command->start_feature;

	memset(cdb, 0, sizeof(cdb));

	cdb[0] = MMC_OPCODE_GET_CONFIG;
	cdb[1] = rt;
	cdb[2] = (uint8_t)(uint16_to_le(start_feature) >> 8);
	cdb[3] = (uint8_t)((uint16_to_le(start_feature) << 8) >> 8);
	cdb[8] = 8; /* enough to get feature descriptor header */

	mmc_response = malloc_aligned(cdb[8], alignment_mask);

	if (mmc_response == 0) {
		return(E_OUTOFMEMORY);
	}

	error = optcl_device_command_execute(
		device, 
		cdb, 
		sizeof(cdb), 
		mmc_response, 
		cdb[8]
		);

	if (FAILED(error)) {
		free_aligned(mmc_response);
		return(error);
	}

	nresponse0 = malloc(sizeof(optcl_mmc_response_get_configuration));

	if (nresponse0 == 0) {
		free_aligned(mmc_response);
		return(E_OUTOFMEMORY);
	}

	memset(nresponse0, 0, sizeof(optcl_mmc_response_get_configuration));

	error = optcl_list_create(0, &nresponse0->descriptors);

	if (FAILED(error)) {
		free(nresponse0);
		free_aligned(mmc_response);
		return(error);
	}

	/*
	 * Set new data length and execute command
	 * 
	 * NOTE that in MMC-5 standard, the entire set of defined 
	 * feature descriptors amounts to less than 1 KB.
	 */

	data_length = uint32_from_be(*(int32_t*)&mmc_response[0]);

	free_aligned(mmc_response);
	
	nresponse0->data_length = data_length;

	if (max_transfer_len > MAX_GET_CONFIG_TRANSFER_LEN)
		max_transfer_len = MAX_GET_CONFIG_TRANSFER_LEN;

	do {
		transfer_size = (data_length > (int32_t)max_transfer_len)
			? max_transfer_len
			: data_length;

		data_length -= max_transfer_len;

		/*
		 * Get the next chunk of data
		 */

		cdb[1] = rt;
		cdb[2] = (uint8_t)(uint16_to_le(start_feature) >> 8);
		cdb[3] = (uint8_t)((uint16_to_le(start_feature) << 8) >> 8);
		cdb[7] = (uint8_t)(uint16_to_le((uint16_t)transfer_size) >> 8);
		cdb[8] = (uint8_t)((uint16_to_le((uint16_t)transfer_size) << 8) >> 8);

		mmc_response = malloc_aligned(transfer_size, alignment_mask);

		if (mmc_response == 0) {
			error = E_OUTOFMEMORY;
			break;
		}

		memset(mmc_response, 0, transfer_size);

		error = optcl_device_command_execute(
			device,
			cdb,
			sizeof(cdb),
			mmc_response,
			transfer_size
			);

		if (FAILED(error)) {
			free_aligned(mmc_response);
			break;
		}

		rt = MMC_GET_CONFIG_RT_FROM;

		/*
		 * Process current chunk of data
		 */

		error = optcl_parse_get_configuration_data(
			mmc_response, 
			transfer_size, 
			&nresponse1
			);

		if (FAILED(error)) {
			free_aligned(mmc_response);
			break;
		}

		error = optcl_list_get_tail_pos(nresponse1->descriptors, &it);

		if (FAILED(error)) {
			destroy_error = optcl_list_destroy(nresponse1->descriptors, True);
			error = (FAILED(destroy_error)) ? destroy_error : error;
			free_aligned(mmc_response);
			free(nresponse1);
			break;
		}

		error = optcl_list_get_at_pos(nresponse1->descriptors, it, (const pptr_t)&descriptor);

		if (FAILED(error)) {
			destroy_error = optcl_list_destroy(nresponse1->descriptors, True);
			error = (FAILED(destroy_error)) ? destroy_error : error;
			free_aligned(mmc_response);
			free(nresponse1);
			break;
		}

		start_feature = descriptor->feature_code + 1;

		nresponse0->current_profile = nresponse1->current_profile;
		
		error = optcl_list_append(nresponse0->descriptors, nresponse1->descriptors);

		if (FAILED(error)) {
			destroy_error = optcl_list_destroy(nresponse1->descriptors, True);
			error = (FAILED(destroy_error)) ? destroy_error : error;
			free_aligned(mmc_response);
			free(nresponse1);
			break;
		}

		error = optcl_list_destroy(nresponse1->descriptors, False);

		if (FAILED(error)) {
			free_aligned(mmc_response);
			free(nresponse1);
			break;
		}

		free(nresponse1);
		free_aligned(mmc_response);

	} while(data_length > 0);

	if (FAILED(error)) {
		destroy_error = optcl_list_destroy(nresponse0->descriptors, False);
		free(nresponse0);
		return(SUCCEEDED(destroy_error) ? error : destroy_error);
	}

	*response = nresponse0;

	return(error);
}

RESULT optcl_command_inquiry(const optcl_device *device, 
			     const optcl_mmc_inquiry *command, 
			     optcl_mmc_response_inquiry **response)
{
	RESULT error;
	RESULT destroy_error;
	uint8_t cdb[6];
	ptr_t mmc_response = 0;
	uint32_t alignment_mask;
	optcl_adapter *adapter;
	optcl_mmc_response_inquiry *nresponse;

	assert(device != 0);
	assert(command != 0);
	assert(response != 0);

	if (device == 0|| command == 0 || response == 0) {
		return(E_INVALIDARG);
	}

	assert(command->evpd == 0);
	assert(command->page_code == 0);

	if (command->evpd != 0 || command->page_code != 0) {
		return(E_INVALIDARG);
	}

	error = optcl_device_get_adapter(device, &adapter);

	if (FAILED(error)) {
		return(error);
	}

	error = optcl_adapter_get_alignment_mask(adapter, &alignment_mask);

	if (FAILED(error)) {
		destroy_error = optcl_adapter_destroy(adapter);
		return(SUCCEEDED(destroy_error) ? error : destroy_error);
	}

	error = optcl_adapter_destroy(adapter);

	if (FAILED(error)) {
		return(error);
	}

	/*
	 * Execute command just to get additional length
	 */

	memset(cdb, 0, sizeof(cdb));

	cdb[0] = MMC_OPCODE_INQUIRY;
	cdb[4] = 5; /* the allocation length should be at least five */

	mmc_response = malloc_aligned(cdb[4], alignment_mask);

	if (mmc_response == 0) {
		return(E_OUTOFMEMORY);
	}

	/* Get standard inquiry data additional length */
	error = optcl_device_command_execute(
		device, 
		cdb, 
		sizeof(cdb), 
		mmc_response,
		cdb[4]
		);

	if (FAILED(error)) {
		free_aligned(mmc_response);
		return(error);
	}

	/* Set standard inquiry data length */
	cdb[4] = mmc_response[4] + 4;

	free_aligned(mmc_response);

	mmc_response = malloc_aligned((size_t)cdb[4], alignment_mask);

	if (mmc_response == 0) {
		return(E_OUTOFMEMORY);
	}

	/* Get standard inquiry data */
	error = optcl_device_command_execute(
		device, 
		cdb, 
		sizeof(cdb), 
		mmc_response,
		cdb[4]
		);

	if (FAILED(error)) {
		free_aligned(mmc_response);
		return(error);
	}

	error = optcl_parse_inquiry_data(mmc_response, cdb[4], &nresponse);

	if (SUCCEEDED(error)) {
		*response = nresponse;
	}

	free_aligned(mmc_response);

	return(error);
}
