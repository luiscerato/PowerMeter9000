#include "json_writer.h"

#include <stdlib.h>
#include <string.h>

JsonWriter::JsonWriter(char* buffer, size_t capacity)
	: buffer_(buffer), capacity_(capacity)
{
	if (buffer_ == nullptr || capacity_ == 0) {
		ok_ = false;
		return;
	}
	buffer_[0] = '\0';
}

JsonWriter::JsonWriter(size_t capacity)
	: capacity_(capacity), ownsBuffer_(true)
{
	buffer_ = static_cast<char*>(malloc(capacity_));
	if (buffer_ == nullptr || capacity_ == 0) {
		ok_ = false;
		return;
	}
	buffer_[0] = '\0';
}

JsonWriter::~JsonWriter()
{
	if (ownsBuffer_ && buffer_ != nullptr)
		free(buffer_);
}

bool JsonWriter::append(const char* text)
{
	if (!ok_ || buffer_ == nullptr || text == nullptr)
		return fail();

	const size_t textLength = strlen(text);
	if (length_ + textLength >= capacity_) {
		if (capacity_ > 0)
			buffer_[capacity_ - 1] = '\0';
		return fail();
	}

	memcpy(buffer_ + length_, text, textLength + 1);
	length_ += textLength;
	return true;
}

bool JsonWriter::appendFormat(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const bool result = appendFormatArgs(format, args);
	va_end(args);
	return result;
}

bool JsonWriter::appendQuoted(const char* value)
{
	if (!append("\""))
		return false;

	for (const uint8_t* current = reinterpret_cast<const uint8_t*>(value ? value : ""); ok_ && *current; ++current) {
		switch (*current) {
		case '"': append("\\\""); break;
		case '\\': append("\\\\"); break;
		case '\n': append("\\n"); break;
		case '\r': append("\\r"); break;
		case '\t': append("\\t"); break;
		default:
			if (*current < 0x20)
				appendFormat("\\u%04x", *current);
			else
				appendFormat("%c", *current);
			break;
		}
	}

	return append("\"");
}

bool JsonWriter::objBegin()
{
	if (depth_ > 0 && !appendValuePrefix())
		return false;
	return append("{") && pushContext(ContextType::Object);
}

bool JsonWriter::objBegin(const char* name)
{
	return appendObjectFieldPrefix(name) && append("{") && pushContext(ContextType::Object);
}

bool JsonWriter::objEnd()
{
	return popContext(ContextType::Object, '}');
}

bool JsonWriter::arrBegin()
{
	if (depth_ > 0 && !appendValuePrefix())
		return false;
	return append("[") && pushContext(ContextType::Array);
}

bool JsonWriter::arrBegin(const char* name)
{
	return appendObjectFieldPrefix(name) && append("[") && pushContext(ContextType::Array);
}

bool JsonWriter::arrEnd()
{
	return popContext(ContextType::Array, ']');
}

bool JsonWriter::objAppendString(const char* name, const char* value)
{
	return appendObjectFieldPrefix(name) && appendQuoted(value);
}

bool JsonWriter::objAppendBool(const char* name, bool value)
{
	return appendObjectFieldPrefix(name) && append(value ? "true" : "false");
}

bool JsonWriter::objAppendInt(const char* name, int32_t value)
{
	return appendObjectFieldPrefix(name) && appendFormat("%ld", static_cast<long>(value));
}

bool JsonWriter::objAppendUInt(const char* name, uint32_t value)
{
	return appendObjectFieldPrefix(name) && appendFormat("%lu", static_cast<unsigned long>(value));
}

bool JsonWriter::objAppendFloat(const char* name, double value, uint8_t decimals)
{
	return appendObjectFieldPrefix(name) && appendFormat("%.*f", decimals, value);
}

bool JsonWriter::objAppendNull(const char* name)
{
	return appendObjectFieldPrefix(name) && append("null");
}

bool JsonWriter::objAppendJson(const char* name, const JsonWriter& value)
{
	if (!value.valid())
		return fail();
	return objAppendRaw(name, value.c_str());
}

bool JsonWriter::objAppendRaw(const char* name, const char* rawJson)
{
	return appendObjectFieldPrefix(name) && append(rawJson);
}

bool JsonWriter::objAppendFloatArray(const char* name, uint8_t count, uint8_t decimals, ...)
{
	if (!arrBegin(name))
		return false;
	va_list args;
	va_start(args, decimals);
	const bool result = appendFloatArrayValues(count, decimals, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::objAppendIntArray(const char* name, uint8_t count, ...)
{
	if (!arrBegin(name))
		return false;
	va_list args;
	va_start(args, count);
	const bool result = appendIntArrayValues(count, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::objAppendUIntArray(const char* name, uint8_t count, ...)
{
	if (!arrBegin(name))
		return false;
	va_list args;
	va_start(args, count);
	const bool result = appendUIntArrayValues(count, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::objAppendBoolArray(const char* name, uint8_t count, ...)
{
	if (!arrBegin(name))
		return false;
	va_list args;
	va_start(args, count);
	const bool result = appendBoolArrayValues(count, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::objAppendStringArray(const char* name, uint8_t count, ...)
{
	if (!arrBegin(name))
		return false;
	va_list args;
	va_start(args, count);
	const bool result = appendStringArrayValues(count, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::objAppendFloatArray(const char* name, const float* values, uint8_t count, uint8_t decimals)
{
	if (values == nullptr || !arrBegin(name))
		return fail();
	for (uint8_t i = 0; i < count; ++i) {
		if (!arrAppendFloat(values[i], decimals))
			return false;
	}
	return arrEnd();
}

bool JsonWriter::objAppendDoubleArray(const char* name, const double* values, uint8_t count, uint8_t decimals)
{
	if (values == nullptr || !arrBegin(name))
		return fail();
	for (uint8_t i = 0; i < count; ++i) {
		if (!arrAppendFloat(values[i], decimals))
			return false;
	}
	return arrEnd();
}

bool JsonWriter::objAppendIntArray(const char* name, const int32_t* values, uint8_t count)
{
	if (values == nullptr || !arrBegin(name))
		return fail();
	for (uint8_t i = 0; i < count; ++i) {
		if (!arrAppendInt(values[i]))
			return false;
	}
	return arrEnd();
}

bool JsonWriter::objAppendUIntArray(const char* name, const uint32_t* values, uint8_t count)
{
	if (values == nullptr || !arrBegin(name))
		return fail();
	for (uint8_t i = 0; i < count; ++i) {
		if (!arrAppendUInt(values[i]))
			return false;
	}
	return arrEnd();
}

bool JsonWriter::objAppendBoolArray(const char* name, const bool* values, uint8_t count)
{
	if (values == nullptr || !arrBegin(name))
		return fail();
	for (uint8_t i = 0; i < count; ++i) {
		if (!arrAppendBool(values[i]))
			return false;
	}
	return arrEnd();
}

bool JsonWriter::arrAppendString(const char* value)
{
	return appendValuePrefix() && appendQuoted(value);
}

bool JsonWriter::arrAppendBool(bool value)
{
	return appendValuePrefix() && append(value ? "true" : "false");
}

bool JsonWriter::arrAppendInt(int32_t value)
{
	return appendValuePrefix() && appendFormat("%ld", static_cast<long>(value));
}

bool JsonWriter::arrAppendUInt(uint32_t value)
{
	return appendValuePrefix() && appendFormat("%lu", static_cast<unsigned long>(value));
}

bool JsonWriter::arrAppendFloat(double value, uint8_t decimals)
{
	return appendValuePrefix() && appendFormat("%.*f", decimals, value);
}

bool JsonWriter::arrAppendNull()
{
	return appendValuePrefix() && append("null");
}

bool JsonWriter::arrAppendJson(const JsonWriter& value)
{
	if (!value.valid())
		return fail();
	return arrAppendRaw(value.c_str());
}

bool JsonWriter::arrAppendRaw(const char* rawJson)
{
	return appendValuePrefix() && append(rawJson);
}

bool JsonWriter::arrAppendFloatArray(uint8_t count, uint8_t decimals, ...)
{
	if (!arrBegin())
		return false;
	va_list args;
	va_start(args, decimals);
	const bool result = appendFloatArrayValues(count, decimals, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::arrAppendIntArray(uint8_t count, ...)
{
	if (!arrBegin())
		return false;
	va_list args;
	va_start(args, count);
	const bool result = appendIntArrayValues(count, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::arrAppendUIntArray(uint8_t count, ...)
{
	if (!arrBegin())
		return false;
	va_list args;
	va_start(args, count);
	const bool result = appendUIntArrayValues(count, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::arrAppendBoolArray(uint8_t count, ...)
{
	if (!arrBegin())
		return false;
	va_list args;
	va_start(args, count);
	const bool result = appendBoolArrayValues(count, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::arrAppendStringArray(uint8_t count, ...)
{
	if (!arrBegin())
		return false;
	va_list args;
	va_start(args, count);
	const bool result = appendStringArrayValues(count, args);
	va_end(args);
	return result && arrEnd();
}

bool JsonWriter::fail()
{
	ok_ = false;
	return false;
}

bool JsonWriter::appendFormatArgs(const char* format, va_list args)
{
	if (!ok_ || buffer_ == nullptr || format == nullptr || length_ >= capacity_)
		return fail();

	va_list copy;
	va_copy(copy, args);
	const int written = vsnprintf(buffer_ + length_, capacity_ - length_, format, copy);
	va_end(copy);

	if (written < 0 || static_cast<size_t>(written) >= capacity_ - length_) {
		buffer_[capacity_ - 1] = '\0';
		return fail();
	}

	length_ += static_cast<size_t>(written);
	return true;
}

bool JsonWriter::inContext(ContextType type) const
{
	return depth_ > 0 && stack_[depth_ - 1].type == type;
}

bool JsonWriter::appendValuePrefix()
{
	if (!inContext(ContextType::Array))
		return fail();

	Context& context = stack_[depth_ - 1];
	if (!context.first && !append(","))
		return false;
	context.first = false;
	return true;
}

bool JsonWriter::appendObjectFieldPrefix(const char* name)
{
	if (!inContext(ContextType::Object) || name == nullptr || name[0] == '\0')
		return fail();

	Context& context = stack_[depth_ - 1];
	if (!context.first && !append(","))
		return false;
	context.first = false;
	return appendQuoted(name) && append(":");
}

bool JsonWriter::pushContext(ContextType type)
{
	if (depth_ >= MaxDepth)
		return fail();

	stack_[depth_].type = type;
	stack_[depth_].first = true;
	depth_++;
	return true;
}

bool JsonWriter::popContext(ContextType type, char closeChar)
{
	if (!inContext(type))
		return fail();

	if (!appendFormat("%c", closeChar))
		return false;
	depth_--;
	return true;
}

bool JsonWriter::appendFloatArrayValues(uint8_t count, uint8_t decimals, va_list args)
{
	for (uint8_t i = 0; i < count; ++i) {
		const double value = va_arg(args, double);
		if (!arrAppendFloat(value, decimals))
			return false;
	}
	return true;
}

bool JsonWriter::appendIntArrayValues(uint8_t count, va_list args)
{
	for (uint8_t i = 0; i < count; ++i) {
		const int value = va_arg(args, int);
		if (!arrAppendInt(value))
			return false;
	}
	return true;
}

bool JsonWriter::appendUIntArrayValues(uint8_t count, va_list args)
{
	for (uint8_t i = 0; i < count; ++i) {
		const unsigned int value = va_arg(args, unsigned int);
		if (!arrAppendUInt(value))
			return false;
	}
	return true;
}

bool JsonWriter::appendBoolArrayValues(uint8_t count, va_list args)
{
	for (uint8_t i = 0; i < count; ++i) {
		const int value = va_arg(args, int);
		if (!arrAppendBool(value != 0))
			return false;
	}
	return true;
}

bool JsonWriter::appendStringArrayValues(uint8_t count, va_list args)
{
	for (uint8_t i = 0; i < count; ++i) {
		const char* value = va_arg(args, const char*);
		if (!arrAppendString(value))
			return false;
	}
	return true;
}
