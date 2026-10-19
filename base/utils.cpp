#include "utils.h"

#include <stdio.h>

namespace {

// Convierte un caracter hexadecimal a su valor numerico.
// value es el caracter ASCII a convertir.
int hexValue(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}

// Devuelve true si el caracter puede ir sin escape en urlencoded.
// value es el byte ASCII a evaluar.
bool isPlainUrlChar(uint8_t value)
{
	return (value >= 'A' && value <= 'Z')
		|| (value >= 'a' && value <= 'z')
		|| (value >= '0' && value <= '9')
		|| value == '-'
		|| value == '_'
		|| value == '.'
		|| value == '~';
}

}

// Prepara el token para reutilizar memoria durante el parseo.
// keyCapacity y valueCapacity son los tamanos iniciales reservados.
UrlFormToken::UrlFormToken(size_t keyCapacity, size_t valueCapacity)
{
	reserve(keyCapacity, valueCapacity);
}

// Reserva memoria para clave y valor sin cambiar su contenido actual.
// keyCapacity y valueCapacity son los tamanos esperados.
void UrlFormToken::reserve(size_t keyCapacity, size_t valueCapacity)
{
	key.reserve(keyCapacity);
	value.reserve(valueCapacity);
}

// Carga el texto application/x-www-form-urlencoded a recorrer.
// text es el formulario origen y se copia internamente.
bool UrlFormDecoder::load(const char* text)
{
	text_ = text != nullptr ? text : "";
	position_ = 0;
	loaded_ = true;
	ok_ = true;
	return true;
}

// Carga el texto application/x-www-form-urlencoded a recorrer.
// text es el formulario origen y se copia internamente.
bool UrlFormDecoder::load(const String& text)
{
	return load(text.c_str());
}

// Devuelve el siguiente par clave/valor ya decodificado.
// token recibe los textos y puede reutilizarse en cada llamada.
bool UrlFormDecoder::next(UrlFormToken& token)
{
	if (!loaded_ || !ok_)
		return false;

	const size_t length = text_.length();
	while (position_ < length && text_[position_] == '&')
		position_++;

	if (position_ >= length)
		return false;

	const size_t pairStart = position_;
	while (position_ < length && text_[position_] != '&')
		position_++;
	const size_t pairEnd = position_;

	size_t separator = pairStart;
	while (separator < pairEnd && text_[separator] != '=')
		separator++;

	token.key = "";
	token.value = "";
	if (!decodeRange(pairStart, separator, token.key)) {
		ok_ = false;
		return false;
	}

	if (separator < pairEnd) {
		if (!decodeRange(separator + 1, pairEnd, token.value)) {
			ok_ = false;
			return false;
		}
	}

	return true;
}

// Decodifica un fragmento urlencoded en output.
// start y end delimitan el fragmento dentro del texto cargado.
bool UrlFormDecoder::decodeRange(size_t start, size_t end, String& output)
{
	output = "";
	output.reserve(end > start ? end - start : 0);

	for (size_t i = start; i < end; ++i) {
		const char current = text_[i];
		if (current == '+') {
			output += ' ';
			continue;
		}

		if (current == '%') {
			if (i + 2 >= end)
				return false;
			const int high = hexValue(text_[i + 1]);
			const int low = hexValue(text_[i + 2]);
			if (high < 0 || low < 0)
				return false;
			output += static_cast<char>((high << 4) | low);
			i += 2;
			continue;
		}

		output += current;
	}

	return true;
}

// Prepara el encoder y reserva memoria inicial para el texto generado.
// capacity es el tamano esperado del formulario codificado.
UrlFormEncoder::UrlFormEncoder(size_t capacity)
{
	text_.reserve(capacity);
}

// Borra el formulario actual y deja el encoder listo para reutilizar.
void UrlFormEncoder::clear()
{
	text_ = "";
}

// Agrega un par clave/valor codificando ambos textos.
// key y value son textos sin codificar.
bool UrlFormEncoder::append(const char* key, const char* value)
{
	if (key == nullptr || key[0] == '\0' || value == nullptr)
		return false;

	if (text_.length() > 0)
		text_ += '&';

	if (!appendEncoded(key))
		return false;
	text_ += '=';
	return appendEncoded(value);
}

// Agrega un par clave/valor desde String.
// key y value son textos sin codificar.
bool UrlFormEncoder::append(const String& key, const String& value)
{
	return append(key.c_str(), value.c_str());
}

// Agrega un par clave/valor entero con signo.
// key es la clave sin codificar y value es el numero a escribir.
bool UrlFormEncoder::appendInt(const char* key, int32_t value)
{
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(value));
	return append(key, buffer);
}

// Agrega un par clave/valor entero sin signo.
// key es la clave sin codificar y value es el numero a escribir.
bool UrlFormEncoder::appendUInt(const char* key, uint32_t value)
{
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(value));
	return append(key, buffer);
}

// Agrega un par clave/valor float.
// key es la clave sin codificar, value el numero y decimals los decimales.
bool UrlFormEncoder::appendFloat(const char* key, double value, uint8_t decimals)
{
	char format[8];
	char buffer[32];
	snprintf(format, sizeof(format), "%%.%uf", decimals);
	snprintf(buffer, sizeof(buffer), format, value);
	return append(key, buffer);
}

// Agrega texto codificado al formulario actual.
// value es el texto sin codificar.
bool UrlFormEncoder::appendEncoded(const char* value)
{
	static const char hex[] = "0123456789ABCDEF";

	for (const uint8_t* current = reinterpret_cast<const uint8_t*>(value); *current != 0; ++current) {
		if (isPlainUrlChar(*current)) {
			text_ += static_cast<char>(*current);
		}
		else if (*current == ' ') {
			text_ += '+';
		}
		else {
			text_ += '%';
			text_ += hex[*current >> 4];
			text_ += hex[*current & 0x0F];
		}
	}

	return true;
}
