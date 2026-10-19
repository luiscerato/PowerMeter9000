#pragma once

#include <Arduino.h>

struct UrlFormToken {
	String key;
	String value;

	// Prepara el token para reutilizar memoria durante el parseo.
	// keyCapacity y valueCapacity son los tamanos iniciales reservados.
	UrlFormToken(size_t keyCapacity = 32, size_t valueCapacity = 128);

	// Reserva memoria para clave y valor sin cambiar su contenido actual.
	// keyCapacity y valueCapacity son los tamanos esperados.
	void reserve(size_t keyCapacity, size_t valueCapacity);
};

class UrlFormDecoder {
public:
	// Carga el texto application/x-www-form-urlencoded a recorrer.
	// text es el formulario origen y se copia internamente.
	bool load(const char* text);

	// Carga el texto application/x-www-form-urlencoded a recorrer.
	// text es el formulario origen y se copia internamente.
	bool load(const String& text);

	// Devuelve el siguiente par clave/valor ya decodificado.
	// token recibe los textos y puede reutilizarse en cada llamada.
	bool next(UrlFormToken& token);

	// Devuelve true si no hubo errores de formato durante el parseo.
	bool ok() const { return ok_; }

private:
	String text_;
	size_t position_ = 0;
	bool loaded_ = false;
	bool ok_ = true;

	// Decodifica un fragmento urlencoded en output.
	// start y end delimitan el fragmento dentro del texto cargado.
	bool decodeRange(size_t start, size_t end, String& output);
};

class UrlFormEncoder {
public:
	// Prepara el encoder y reserva memoria inicial para el texto generado.
	// capacity es el tamano esperado del formulario codificado.
	explicit UrlFormEncoder(size_t capacity = 256);

	// Borra el formulario actual y deja el encoder listo para reutilizar.
	void clear();

	// Agrega un par clave/valor codificando ambos textos.
	// key y value son textos sin codificar.
	bool append(const char* key, const char* value);

	// Agrega un par clave/valor desde String.
	// key y value son textos sin codificar.
	bool append(const String& key, const String& value);

	// Agrega un par clave/valor entero con signo.
	// key es la clave sin codificar y value es el numero a escribir.
	bool appendInt(const char* key, int32_t value);

	// Agrega un par clave/valor entero sin signo.
	// key es la clave sin codificar y value es el numero a escribir.
	bool appendUInt(const char* key, uint32_t value);

	// Agrega un par clave/valor float.
	// key es la clave sin codificar, value el numero y decimals los decimales.
	bool appendFloat(const char* key, double value, uint8_t decimals = 3);

	// Devuelve el formulario codificado generado.
	const String& text() const { return text_; }

	// Devuelve el formulario codificado como const char*.
	const char* c_str() const { return text_.c_str(); }

private:
	String text_;

	// Agrega texto codificado al formulario actual.
	// value es el texto sin codificar.
	bool appendEncoded(const char* value);
};
