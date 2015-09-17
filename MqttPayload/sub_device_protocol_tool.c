//  Copyright (c) 2015 Pando. All rights reserved.
//  PtotoBuf:   ProtocolBuffer.h
//
//  Create By ZhaoWenwu On 15/01/24.

#include "sub_device_protocol_tool.h"

#define DEFAULT_TLV_BLOCK_SIZE 128

/***************** Local types ****************/
struct params_block_indicator
{
    uint16_t tlv_count;     //the count of tlv in TLVs
    uint16_t handled_length;
};

struct property_indicator
{
    int position;
};
/**********************************************/

static struct property_indicator s_current_property;
static struct params_block_indicator s_current_param;
struct sub_device_base_params base_params;	//子设备的基本参数
static uint16_t current_tlv_block_size = 0;	//当前信息区的大小，包含count的大小
static uint16_t tlv_block_buffer_size;

static struct TLVs FUNCTION_ATTRIBUTE *get_next_property(struct pando_property *next_property, struct pando_property *property_body);
static uint8_t FUNCTION_ATTRIBUTE is_tlv_need_length(uint16_t type);
static uint8_t FUNCTION_ATTRIBUTE get_type_length(uint16_t type);
/* the way to create event and command is exact same, except type */
static struct sub_device_buffer * FUNCTION_ATTRIBUTE create_payload_package(
    uint16_t event_num, uint16_t flags, 
    uint16_t priority, struct TLVs *event_params,
    uint16_t payload_type);


// we must maintain the position of tlv for next get operation
// handled_length means all the tlv has been handled 
static void FUNCTION_ATTRIBUTE cal_current_position(uint16_t type, uint16_t len)
{
    s_current_param.tlv_count--;
    s_current_param.handled_length += len
        + sizeof(type) + sizeof(len) * is_tlv_need_length(type);
    
    if (s_current_param.tlv_count == 0)
    {
        //if is handle data property's params
        if (s_current_property.position != 0)   //mean has already decode with property
        {
            s_current_property.position += s_current_param.handled_length;
        }

        //all tlv param has been handled.
        s_current_param.tlv_count= 0;
        s_current_param.handled_length= 0;
    }
}

static struct TLV FUNCTION_ATTRIBUTE *get_current_tlv(struct TLVs *params)
{
    if (s_current_param.handled_length == 0) //first tlv
    {
        s_current_param.tlv_count = net16_to_host(params->count);        
    }
    
    return (struct TLV *)((uint8_t *)params + sizeof(struct TLVs) 
        + s_current_param.handled_length);   
}

static void FUNCTION_ATTRIBUTE get_value(struct TLVs *params, void *val)
{
    uint16_t type = 0;
    uint16_t len = 0;

    struct TLV* params_in = get_current_tlv(params);    
    get_tlv_param(params_in, &type, &len, val);
    cal_current_position(type, len);
}

static void FUNCTION_ATTRIBUTE *get_string(struct TLVs *params, uint16_t *len)
{
    uint16_t type = 0;
    void *val = NULL;
    struct TLV* params_in = get_current_tlv(params);
    val = params_in->value;
    *len = get_tlv_len(params_in);
    type = get_tlv_type(params_in);
    cal_current_position(type, *len);

    return val;
}

int FUNCTION_ATTRIBUTE init_sub_device(struct sub_device_base_params params)
{
	base_params.data_sequence = params.data_sequence;
	base_params.event_sequence = params.event_sequence;
	base_params.command_sequence = params.command_sequence;
	return 0;
}

struct TLVs * FUNCTION_ATTRIBUTE create_params_block()
{
	struct TLVs *tlv_block = NULL;
    uint8_t need_length;

	current_tlv_block_size = 0;		//确保每次新建tlv信息区时，信息区大小的计数都是正确的

    tlv_block_buffer_size = DEFAULT_TLV_BLOCK_SIZE;
	tlv_block = (struct TLVs *)pd_malloc(tlv_block_buffer_size);
    
	if (tlv_block == NULL)
	{
		return NULL;
	}
	tlv_block->count = 0;	//初始化个数为0
	current_tlv_block_size = sizeof(struct TLVs);		//没有添加param时，block的内存占用大小仅仅是个数

	return tlv_block;
}

int FUNCTION_ATTRIBUTE add_next_param(struct TLVs *params_block, uint16_t next_type, uint16_t next_length, void *next_value)
{
	uint16_t type;
    uint16_t conver_length;
    uint8_t need_length;
    uint8_t tlv_length;
	uint8_t tmp_value[8];
    uint8_t *tlv_position;
	struct TLVs *new_property_block = NULL;
	uint16_t current_count = net16_to_host(params_block->count);	//由于create的时候就对该值进行了端转换，因此需要回转

    need_length = is_tlv_need_length(next_type);
    if (1 == need_length)
    {
        
    }
    else if (0 == need_length)
    {
        if(next_length != get_type_length(next_type))
        {
            printf("Param type dismatch with the input length\n");
            return -1;
        }
    }
    else
    {
        return -1;
    }
    
	//信息区扩容
	if (current_tlv_block_size + next_length + sizeof(struct TLV) 
        - (!need_length) * sizeof(next_length) > tlv_block_buffer_size)
	{
		tlv_block_buffer_size = tlv_block_buffer_size + DEFAULT_TLV_BLOCK_SIZE + next_length;
		new_property_block = (struct TLVs *)pd_malloc(tlv_block_buffer_size);
		pd_memcpy(new_property_block, params_block, current_tlv_block_size);
		pd_free(params_block);
		params_block = new_property_block;
	} 

	current_count++;
	tlv_position = (uint8_t *)params_block + current_tlv_block_size;

	//将count保存为网络字节序，便于创建事件或数据包时直接赋值
	params_block->count = host16_to_net(current_count);
	
	//复制tlv内容，并大小端转换,修正tlv长度和type的设置方式，解决跨字错误
	type = host16_to_net(next_type);
	pd_memcpy(tlv_position, &type, sizeof(type));
    tlv_position += sizeof(type);

    
	if (need_length)
	{
        conver_length = host16_to_net(next_length);
	    pd_memcpy(tlv_position, &conver_length, sizeof(conver_length));
        tlv_position += sizeof(conver_length);
    }
	

	switch(next_type)
	{
	    case TLV_TYPE_FLOAT64 :
		    *((double *)tmp_value) = host64f_to_net(*((uint64_t *)next_value));
		    pd_memcpy(tlv_position, tmp_value, next_length);
		    break;
    	case TLV_TYPE_FLOAT32 :
    		*((float *)tmp_value) = host32f_to_net(*((float *)next_value));
    		pd_memcpy(tlv_position, tmp_value, next_length);
    			break;
	    case TLV_TYPE_INT16:
		    *((int16_t *)tmp_value) = host16_to_net(*((int16_t *)next_value));
    		pd_memcpy(tlv_position, tmp_value, next_length);
	    	break;
    	case TLV_TYPE_UINT16:
	    	*((uint16_t *)tmp_value) = host16_to_net(*((uint16_t *)next_value));
		    pd_memcpy(tlv_position, tmp_value, next_length);
		    break;
    	case TLV_TYPE_INT32:
	    	*((int32_t *)tmp_value) = host32_to_net(*((int32_t *)next_value));
		    pd_memcpy(tlv_position, tmp_value, next_length);
		    break;
	    case TLV_TYPE_UINT32:
		    *((uint32_t *)tmp_value) = host32_to_net(*((uint32_t *)next_value));
		    pd_memcpy(tlv_position, tmp_value, next_length);
    	    break;
	    case TLV_TYPE_UINT64:
    		*((uint64_t *)tmp_value) = host64_to_net(*((uint64_t *)next_value));
	    	pd_memcpy(tlv_position, tmp_value, next_length);
		    break;
    	case TLV_TYPE_INT64:
	    	*((int64_t *)tmp_value) = host64_to_net(*((int64_t *)next_value));
		    pd_memcpy(tlv_position, tmp_value, next_length);
		    break;
	    default:
	        pd_memcpy(tlv_position, next_value, next_length);
		    break;
	}

	current_tlv_block_size = current_tlv_block_size + next_length 
        + sizeof(struct TLV) - (!need_length) * sizeof(next_length);
    
	return 0;
}

struct sub_device_buffer * FUNCTION_ATTRIBUTE create_event_package(
    uint16_t event_num, uint16_t flags, 
    uint16_t priority, struct TLVs *event_params)
{
    return create_payload_package(
        event_num, flags, priority, event_params, 
        PAYLOAD_TYPE_EVENT);
}

struct sub_device_buffer *create_command_package(uint16_t cmd_num, 
    uint16_t flags, uint16_t priority, struct TLVs *cmd_params)

{
    return create_payload_package(
        cmd_num, flags, priority, cmd_params, 
        PAYLOAD_TYPE_COMMAND);
}


void FUNCTION_ATTRIBUTE delete_device_package(struct sub_device_buffer *device_buffer)
{
    if (device_buffer != NULL)
    {
        if (device_buffer->buffer != NULL)
        {
            pd_free(device_buffer->buffer);
            device_buffer->buffer = NULL;
        }
    }

    pd_free(device_buffer);
	device_buffer = NULL;
}

void FUNCTION_ATTRIBUTE delete_params_block(struct TLVs *params_block)
{
    if (params_block != NULL)
    {
	    pd_free(params_block);
	    params_block = NULL;

    }
}

struct sub_device_buffer * FUNCTION_ATTRIBUTE create_data_package(uint16_t property_num, uint16_t flags, struct TLVs *data_params)
{
	struct sub_device_buffer *data_buffer = NULL;
	struct pando_property *single_property = NULL;
	struct device_header *header = NULL;
	uint16_t payload_length;
	uint16_t payload_type = PAYLOAD_TYPE_DATA;

	if (data_params == NULL)
	{
		pd_printf("Must create data package with params.\n");
		return NULL;
	}

	data_buffer = (struct sub_device_buffer *)pd_malloc(sizeof(struct sub_device_buffer));

	payload_length = sizeof(struct pando_property) + current_tlv_block_size - sizeof(struct TLVs);		//count 被重复包含了
	data_buffer->buffer_length = payload_length + DEV_HEADER_LEN;
	data_buffer->buffer = (uint8_t *)pd_malloc(data_buffer->buffer_length);
	pd_memset(data_buffer->buffer, 0, data_buffer->buffer_length);

	header = (struct device_header *)data_buffer->buffer;
	single_property = (struct pando_property *)(data_buffer->buffer + DEV_HEADER_LEN);
	
	base_params.data_sequence++;

	header->crc = 0x11;
	header->flags = host16_to_net(flags);
	header->frame_seq = host32_to_net(base_params.data_sequence);
	header->magic = MAGIC_HEAD_SUB_DEVICE;
	header->payload_len = host16_to_net(payload_length);
	header->payload_type = host16_to_net(payload_type);

	single_property->property_num = host16_to_net(property_num);

	pd_memcpy(single_property->params, data_params, current_tlv_block_size);
		
	return data_buffer;
}


struct TLV * FUNCTION_ATTRIBUTE get_sub_device_command(struct sub_device_buffer *device_buffer, struct pando_command *command_body)
{
	struct pando_command *tmp_body = (struct pando_command *)(device_buffer->buffer + DEV_HEADER_LEN);

	//需要对包头进行校验，尚未编写
	struct device_header *head = (struct device_header *)device_buffer->buffer;
	base_params.command_sequence = net32_to_host(head->frame_seq);
    command_body->sub_device_id = net16_to_host(tmp_body->sub_device_id);
/*
	if ((command_body->sub_device_id = net16_to_host(tmp_body->sub_device_id)) != base_params.sub_device_id)
	{
		pd_printf("Local sub device id: %d, receive a command to id : %d.\n", base_params.sub_device_id, command_body->sub_device_id);
		return -1;
	}*/
	command_body->command_id = net16_to_host(tmp_body->command_id);
	command_body->priority = net16_to_host(tmp_body->priority);
	command_body->params->count = net16_to_host(tmp_body->params->count);
	
	return (struct TLV *)(device_buffer->buffer + DEV_HEADER_LEN + sizeof(struct pando_command));
}

uint16_t get_tlv_count(struct TLVs *params_block)
{
    uint16_t count = 0;

    if (params_block == NULL)
    {
        return -1;
    }
    pd_memcpy(&count, &(params_block->count), sizeof(count));
    return count;
}

uint16_t FUNCTION_ATTRIBUTE get_tlv_type(struct TLV *params_in)
{
    uint16_t type = 0;
    pd_memcpy(&type, &(params_in->type), sizeof(params_in->type));
    type = net16_to_host(type);
    return type;
}


uint16_t FUNCTION_ATTRIBUTE get_tlv_len(struct TLV *params_in)
{
    uint8_t need_length;
    uint16_t type = 0;
    uint16_t length = 0;
    void *value_pos = NULL;

    if (params_in == NULL)
    {
        return -1;
    }
    
    pd_memcpy(&type, &(params_in->type), sizeof(params_in->type));
    type = net16_to_host(type);
    
    need_length = is_tlv_need_length(type);
    if (need_length == 1)
    {        
        pd_memcpy(&length, &(params_in->length), sizeof(params_in->length));
        length = net16_to_host(length);
    }
    else
    {
        length = get_type_length(type);
    }

    return length;
}

struct TLV * FUNCTION_ATTRIBUTE get_tlv_value(struct TLV *params_in, void *value)
{
    uint8_t need_length;
    void *value_pos = NULL;
    uint16_t type_p;
    uint16_t length_p;
    uint16_t *type = &type_p;
    uint16_t *length = &length_p;

    pd_memcpy(type, &(params_in->type), sizeof(params_in->type));
    *type = net16_to_host(*type);
    
    need_length = is_tlv_need_length(*type);
    if (need_length == 1)
    {        
        pd_memcpy(length, &(params_in->length), sizeof(params_in->length));
        *length = net16_to_host(*length);
        pd_memcpy(value, params_in->value, *length);
    }
    else
    {
        *length = get_type_length(*type);
        value_pos = (uint8_t *)params_in + sizeof(params_in->type);
        pd_memcpy(value, value_pos, *length);
        switch (*type)
        {
            case TLV_TYPE_FLOAT64:
                net64f_to_host(*(double *)value);
                break;
            case TLV_TYPE_UINT64:
            case TLV_TYPE_INT64:
                net64_to_host(*(uint64_t *)value);
                break;
            case TLV_TYPE_FLOAT32:
                net32f_to_host(*(float *)value);
                break;
            case TLV_TYPE_UINT32:
            case TLV_TYPE_INT32:
                net32_to_host(*(uint32_t *)value);
                break;
            case TLV_TYPE_INT8:
            case TLV_TYPE_UINT8:
            case TLV_TYPE_BOOL:
                break;
            case TLV_TYPE_INT16:
            case TLV_TYPE_UINT16:
                net16_to_host(*(uint16_t *)value);
                break;
            default:
                break;
        }//switch            
    }//need_length == 0
    
	return (struct TLV *)((uint8_t *)params_in + *length + sizeof(struct TLV) 
        - (!need_length) * sizeof(*length));    
}


struct TLV * FUNCTION_ATTRIBUTE get_tlv_param(struct TLV *params_in, uint16_t *type, 
    uint16_t *length, void *value)

{
    uint8_t need_length;
    void *value_pos = NULL;

    pd_memcpy(type, &(params_in->type), sizeof(params_in->type));
    *type = net16_to_host(*type);
    
    need_length = is_tlv_need_length(*type);
    if (need_length == 1)
    {        
        pd_memcpy(length, &(params_in->length), sizeof(params_in->length));
        *length = net16_to_host(*length);
        pd_memcpy(value, params_in->value, *length);
    }
    else
    {
        *length = get_type_length(*type);
        value_pos = (uint8_t *)params_in + sizeof(params_in->type);
        pd_memcpy(value, value_pos, *length);
        switch (*type)
        {
            case TLV_TYPE_FLOAT64:
                net64f_to_host(*(double *)value);
                break;
            case TLV_TYPE_UINT64:
            case TLV_TYPE_INT64:
                net64_to_host(*(uint64_t *)value);
                break;
            case TLV_TYPE_FLOAT32:
                net32f_to_host(*(float *)value);
                break;
            case TLV_TYPE_UINT32:
            case TLV_TYPE_INT32:
                net32_to_host(*(uint32_t *)value);
                break;
            case TLV_TYPE_INT8:
            case TLV_TYPE_UINT8:
            case TLV_TYPE_BOOL:
                break;
            case TLV_TYPE_INT16:
            case TLV_TYPE_UINT16:
                net16_to_host(*(uint16_t *)value);
                break;
            default:
                break;
        }//switch            
    }//need_length == 0
    
	return (struct TLV *)((uint8_t *)params_in + *length + sizeof(struct TLV) 
        - (!need_length) * sizeof(*length));
}

/*******************************************************
 * Description: 
 * param
 * return:
 *********************************************************/ 
struct TLVs * FUNCTION_ATTRIBUTE get_sub_device_property(
    struct sub_device_buffer *device_buffer, 
    struct pando_property *property_body)
{
    struct pando_property *tmp_body = NULL;
    struct device_header *head = NULL;

    //reach the end of buffer
    if (s_current_property.position == device_buffer->buffer_length
        || s_current_property.position > device_buffer->buffer_length)
    {
        s_current_property.position = 0;
        return NULL;
    }
    
    if (s_current_property.position == 0)
    {
        //first property, need handle header length
        s_current_property.position += DEV_HEADER_LEN;
        head = (struct device_header *)device_buffer->buffer;
	    base_params.data_sequence = net32_to_host(head->frame_seq);
    }    

    tmp_body = (struct pando_property *)(device_buffer->buffer
            + s_current_property.position);

    s_current_property.position += sizeof(struct pando_property);
    
	return get_next_property(tmp_body, property_body);
}


struct sub_device_buffer * FUNCTION_ATTRIBUTE create_feedback_package()
{
	struct sub_device_buffer *device_buffer = NULL;
	struct pando_event *event = NULL;
	struct device_header *header;
	uint16_t payload_length;
	uint16_t payload_type = PAYLOAD_TYPE_EVENT;

	device_buffer = (struct sub_device_buffer *)pd_malloc(sizeof(struct sub_device_buffer));

	payload_length = 0;
	device_buffer->buffer = (uint8_t *)pd_malloc(DEV_HEADER_LEN);		
	pd_memset(device_buffer->buffer, 0, DEV_HEADER_LEN);

	device_buffer->buffer_length = DEV_HEADER_LEN;

	header = (struct device_header *)device_buffer->buffer;
	base_params.event_sequence++;

	header->magic = MAGIC_HEAD_SUB_DEVICE;
	header->crc = 0x11;
	header->frame_seq = host32_to_net(base_params.command_sequence);
	header->flags = host16_to_net(0xaabb);
	header->payload_len = host16_to_net(payload_length);
	header->payload_type = host16_to_net(payload_type);

	return device_buffer;
}

uint16_t FUNCTION_ATTRIBUTE get_sub_device_payloadtype(struct sub_device_buffer *package)
{
    struct device_header *head;
 
    if (package == NULL)
	{
		return -1;
	}
    
    head = (struct device_header *)package->buffer;

    return net16_to_host(head->payload_type);
}

uint8_t FUNCTION_ATTRIBUTE is_tlv_need_length(uint16_t type)
{
    switch (type)
    {
        case TLV_TYPE_FLOAT64:
        case TLV_TYPE_FLOAT32:
        case TLV_TYPE_INT8:
        case TLV_TYPE_INT16:
        case TLV_TYPE_INT32:
        case TLV_TYPE_INT64:
        case TLV_TYPE_UINT8:
        case TLV_TYPE_UINT16:
        case TLV_TYPE_UINT32:
        case TLV_TYPE_UINT64:
        case TLV_TYPE_BOOL:
            return 0;
            break;
        case TLV_TYPE_BYTES:
        case TLV_TYPE_URI:
            return 1;
            break;
            
        default:
            printf("Unknow data type.\n");
            return -1;
            break;
    }
    
    return -1;
}

uint8_t FUNCTION_ATTRIBUTE get_type_length(uint16_t type)
{
    switch (type)
    {
        case TLV_TYPE_FLOAT64:
        case TLV_TYPE_UINT64:
        case TLV_TYPE_INT64:
            return 8;
            break;
        case TLV_TYPE_FLOAT32:
        case TLV_TYPE_UINT32:
        case TLV_TYPE_INT32:
            return 4;
            break;
        case TLV_TYPE_INT8:
        case TLV_TYPE_UINT8:
        case TLV_TYPE_BOOL:
            return 1;
        case TLV_TYPE_INT16:
        case TLV_TYPE_UINT16:
            return 2;
            break;
        case TLV_TYPE_BYTES:
        case TLV_TYPE_URI:
            return 0;
            break;
            
        default:
            printf("Unknow data type.\n");
            return -1;
            break;
    }
    
    return -1;
}

int FUNCTION_ATTRIBUTE add_next_property(struct sub_device_buffer *data_package, uint16_t property_num, struct TLVs *next_data_params)
{
    uint8_t *new_buffer = NULL;
    uint8_t *position = NULL;
    struct device_header *hdr = (struct device_header *)data_package->buffer;
    uint16_t payload_len = 0;
    
    uint16_t append_len = sizeof(struct pando_property) + current_tlv_block_size - sizeof(struct TLVs);
    

    if (data_package == NULL)
    {
        return -1;
    }

    /* append payload length */
    payload_len = net16_to_host(hdr->payload_len);
    data_package->buffer_length += append_len;
    payload_len += append_len;
    hdr->payload_len = host16_to_net(payload_len);
    
    new_buffer = (uint8_t *)pd_malloc(data_package->buffer_length);
    pd_memset(new_buffer, 0, data_package->buffer_length);

    /* copy buffer content, skip subdev id, copy property num and tlv params */
    position = new_buffer + data_package->buffer_length - append_len;
    pd_memcpy(new_buffer, data_package->buffer, data_package->buffer_length);

    position += 2;  //sub device id
    property_num = host16_to_net(property_num);
    pd_memcpy(position, &property_num, sizeof(property_num));
    
    position += sizeof(property_num);
    pd_memcpy(position, next_data_params, current_tlv_block_size);  
    pd_free(data_package->buffer);
    data_package->buffer = new_buffer;

    return 0;
}

struct TLVs * FUNCTION_ATTRIBUTE get_next_property(
    struct pando_property *next_property, struct pando_property *property_body)
{
    property_body->sub_device_id = net16_to_host(next_property->sub_device_id);
    property_body->property_num = net16_to_host(next_property->property_num);
    property_body->params->count = net16_to_host(next_property->params->count);


    return (struct TLVs *)((uint8_t *)next_property 
        + sizeof(struct pando_property)
        - sizeof(struct TLVs));
}

int FUNCTION_ATTRIBUTE is_device_file_command(struct sub_device_buffer *device_buffer)
{
    struct device_header *header = (struct device_header *)device_buffer->buffer;

    if (header->flags == 1)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

struct sub_device_buffer * FUNCTION_ATTRIBUTE create_payload_package(
    uint16_t event_num, uint16_t flags, 
    uint16_t priority, struct TLVs *event_params,
    uint16_t type)
{
	struct sub_device_buffer *device_buffer = NULL;
	struct pando_event *event = NULL;
	struct device_header *header;
	uint16_t payload_length;
	uint16_t payload_type = type;

	device_buffer = (struct sub_device_buffer *)pd_malloc(sizeof(struct sub_device_buffer));
	if (event_params != NULL)
	{
		payload_length = sizeof(struct pando_event) + current_tlv_block_size - sizeof(struct TLVs);		//由于pando_event已包含一个tlvs的长度，需要减掉
		device_buffer->buffer = (uint8_t *)pd_malloc(DEV_HEADER_LEN + payload_length);
		pd_memset(device_buffer->buffer, 0, DEV_HEADER_LEN + payload_length);
		pd_memcpy(device_buffer->buffer + DEV_HEADER_LEN + sizeof(struct pando_event) - sizeof(struct TLVs) , 
			event_params, current_tlv_block_size);
	} 
	else
	{
		payload_length = sizeof(struct pando_event);
		device_buffer->buffer = (uint8_t *)pd_malloc(DEV_HEADER_LEN + payload_length);		
		pd_memset(device_buffer->buffer, 0, DEV_HEADER_LEN + payload_length);
	}
	device_buffer->buffer_length = DEV_HEADER_LEN + payload_length;

	header = (struct device_header *)device_buffer->buffer;
	event = (struct pando_event *)(device_buffer->buffer + DEV_HEADER_LEN);
	base_params.event_sequence++;

	header->magic = MAGIC_HEAD_SUB_DEVICE;
	header->crc = 0x11;
	header->frame_seq = host32_to_net(base_params.event_sequence);
	header->flags = host16_to_net(flags);
	header->payload_len = host16_to_net(payload_length);
	header->payload_type = host16_to_net(payload_type);

	event->event_num = host16_to_net(event_num);
    event->priority = host16_to_net(priority);
	
	return device_buffer;

}


/*
    Temp region
*/
uint8_t FUNCTION_ATTRIBUTE get_next_uint8(struct TLVs *params)
{
    uint8_t val = 0;
    get_value(params, &val);    
    return val;
}

uint16_t FUNCTION_ATTRIBUTE get_next_uint16(struct TLVs *params)
{
    uint16_t val = 0;
    get_value(params, &val);    
    return val;

}

uint32_t FUNCTION_ATTRIBUTE get_next_uint32(struct TLVs *params)
{
    uint32_t val = 0;
    get_value(params, &val);    
    return val;

}

uint64_t FUNCTION_ATTRIBUTE get_next_uint64(struct TLVs *params)
{
    uint64_t val = 0;
    get_value(params, &val);    
    return val;

}

int8_t FUNCTION_ATTRIBUTE get_next_int8(struct TLVs *params)
{
    int8_t val = 0;
    get_value(params, &val);    
    return val;
}

int16_t FUNCTION_ATTRIBUTE get_next_int16(struct TLVs *params)
{
    int16_t val = 0;
    get_value(params, &val);    
    return val;

}

int32_t FUNCTION_ATTRIBUTE get_next_int32(struct TLVs *params)
{
    int32_t val = 0;
    get_value(params, &val);    
    return val;

}

int64_t FUNCTION_ATTRIBUTE get_next_int64(struct TLVs *params)
{
    int64_t val = 0;
    get_value(params, &val);    
    return val;

}

float FUNCTION_ATTRIBUTE get_next_float32(struct TLVs *params)
{
    float val = 0;
    get_value(params, &val);    
    return val;

}

double FUNCTION_ATTRIBUTE get_next_float64(struct TLVs *params)
{
    double val = 0;
    get_value(params, &val);    
    return val;

}

uint8_t FUNCTION_ATTRIBUTE get_next_bool(struct TLVs *params)
{
    uint8_t val = 0;
    get_value(params, &val);    
    return val;

}

void FUNCTION_ATTRIBUTE *get_next_uri(struct TLVs *params, uint16_t *length)
{
    return get_string(params,length);
}

void FUNCTION_ATTRIBUTE *get_next_bytes(struct TLVs *params, uint16_t *length)
{
    return get_string(params,length);
}


int    add_next_uint8(struct TLVs *params, uint8_t next_value)
{
    return add_next_param(params, TLV_TYPE_UINT8, get_type_length(TLV_TYPE_INT8), &next_value);
}

int    add_next_uint16(struct TLVs *params, uint16_t next_value)
{
    return add_next_param(params, TLV_TYPE_UINT16, get_type_length(TLV_TYPE_UINT16), &next_value);
}

int    add_next_uint32(struct TLVs *params, uint32_t next_value)
{
    return add_next_param(params, TLV_TYPE_UINT32, get_type_length(TLV_TYPE_UINT32), &next_value);
}

int    add_next_uint64(struct TLVs *params, uint64_t next_value)
{
    return add_next_param(params, TLV_TYPE_UINT64, get_type_length(TLV_TYPE_UINT64), &next_value);
}

int    add_next_int8(struct TLVs *params, int8_t next_value)
{
    return add_next_param(params, TLV_TYPE_INT8, get_type_length(TLV_TYPE_INT8), &next_value);
}

int    add_next_int16(struct TLVs *params, int16_t next_value)
{
    return add_next_param(params, TLV_TYPE_INT16, get_type_length(TLV_TYPE_INT16), &next_value);
}

int    add_next_int32(struct TLVs *params, int32_t next_value)
{
    return add_next_param(params, TLV_TYPE_INT32, get_type_length(TLV_TYPE_INT32), &next_value);
}

int    add_next_int64(struct TLVs *params, int64_t next_value)
{
    return add_next_param(params, TLV_TYPE_INT64, get_type_length(TLV_TYPE_INT64), &next_value);
}

int    add_next_float32(struct TLVs *params, float next_value)
{
    return add_next_param(params, TLV_TYPE_FLOAT32, get_type_length(TLV_TYPE_FLOAT32), &next_value);
}

int    add_next_float64(struct TLVs *params, double next_value)
{
    return add_next_param(params, TLV_TYPE_FLOAT64, get_type_length(TLV_TYPE_FLOAT64), &next_value);
}

int    add_next_bool(struct TLVs *params, uint8_t next_value)
{
    return add_next_param(params, TLV_TYPE_BOOL, get_type_length(TLV_TYPE_BOOL), &next_value);
}

int    add_next_uri(struct TLVs *params, uint16_t length, void *next_value)
{
    return add_next_param(params, TLV_TYPE_URI, length, &next_value);
}

int    add_next_bytes(struct TLVs *params, uint16_t length, void *next_value)
{
    return add_next_param(params, TLV_TYPE_BYTES, length, &next_value);
}
