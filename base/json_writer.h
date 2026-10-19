#pragma once

#include <Arduino.h>
#include <stdarg.h>

class JsonWriter {
public:
	// Usa un buffer externo para construir JSON.
	// buffer recibe el texto generado y capacity indica su tamano total.
	JsonWriter(char* buffer, size_t capacity);

	// Reserva un buffer interno con malloc para construir JSON.
	// capacity indica el tamano total a reservar.
	explicit JsonWriter(size_t capacity);

	// Libera el buffer interno si fue reservado por la clase.
	~JsonWriter();

	JsonWriter(const JsonWriter&) = delete;
	JsonWriter& operator=(const JsonWriter&) = delete;

	// Devuelve true si no hubo errores durante la escritura.
	bool ok() const { return ok_; }

	// Devuelve true si el JSON esta completo, cerrado y sin errores.
	bool valid() const { return ok_ && depth_ == 0 && length_ > 0; }

	// Devuelve el texto JSON generado.
	const char* c_str() const { return buffer_ != nullptr ? buffer_ : ""; }

	// Devuelve la cantidad de caracteres escritos sin contar el terminador.
	size_t length() const { return length_; }

	// Devuelve el tamano total disponible del buffer.
	size_t capacity() const { return capacity_; }

	// Devuelve la cantidad de objetos/arrays abiertos.
	uint8_t depth() const { return depth_; }

	// Agrega texto crudo al JSON.
	// text es copiado sin escaparlo.
	bool append(const char* text);

	// Agrega texto formateado al JSON.
	// format y sus argumentos siguen las reglas de snprintf.
	bool appendFormat(const char* format, ...);

	// Agrega un string JSON entre comillas y escapado.
	// value puede ser nullptr y se escribe como string vacio.
	bool appendQuoted(const char* value);

	// Inicia un objeto JSON.
	// Sin name crea un objeto raiz o un objeto dentro de un array.
	bool objBegin();

	// Inicia un objeto JSON como campo del objeto actual.
	// name es el nombre del campo.
	bool objBegin(const char* name);

	// Cierra el objeto JSON actual.
	bool objEnd();

	// Inicia un array JSON.
	// Sin name crea un array raiz o un array dentro de otro array.
	bool arrBegin();

	// Inicia un array JSON como campo del objeto actual.
	// name es el nombre del campo.
	bool arrBegin(const char* name);

	// Cierra el array JSON actual.
	bool arrEnd();

	// Agrega un campo string al objeto actual.
	bool objAppendString(const char* name, const char* value);

	// Agrega un campo booleano al objeto actual.
	bool objAppendBool(const char* name, bool value);

	// Agrega un campo entero con signo al objeto actual.
	bool objAppendInt(const char* name, int32_t value);

	// Agrega un campo entero sin signo al objeto actual.
	bool objAppendUInt(const char* name, uint32_t value);

	// Agrega un campo float al objeto actual.
	bool objAppendFloat(const char* name, double value, uint8_t decimals = 2);

	// Agrega un campo null al objeto actual.
	bool objAppendNull(const char* name);

	// Agrega un JsonWriter ya cerrado como campo del objeto actual.
	bool objAppendJson(const char* name, const JsonWriter& value);

	// Agrega JSON crudo como campo del objeto actual.
	bool objAppendRaw(const char* name, const char* rawJson);

	// Agrega un array de floats al objeto actual usando argumentos variables.
	bool objAppendFloatArray(const char* name, uint8_t count, uint8_t decimals, ...);

	// Agrega un array de enteros con signo al objeto actual usando argumentos variables.
	bool objAppendIntArray(const char* name, uint8_t count, ...);

	// Agrega un array de enteros sin signo al objeto actual usando argumentos variables.
	bool objAppendUIntArray(const char* name, uint8_t count, ...);

	// Agrega un array de booleanos al objeto actual usando argumentos variables.
	bool objAppendBoolArray(const char* name, uint8_t count, ...);

	// Agrega un array de strings al objeto actual usando argumentos variables.
	bool objAppendStringArray(const char* name, uint8_t count, ...);

	// Agrega un array de floats desde memoria al objeto actual.
	bool objAppendFloatArray(const char* name, const float* values, uint8_t count, uint8_t decimals);

	// Agrega un array de doubles desde memoria al objeto actual.
	bool objAppendDoubleArray(const char* name, const double* values, uint8_t count, uint8_t decimals);

	// Agrega un array de enteros con signo desde memoria al objeto actual.
	bool objAppendIntArray(const char* name, const int32_t* values, uint8_t count);

	// Agrega un array de enteros sin signo desde memoria al objeto actual.
	bool objAppendUIntArray(const char* name, const uint32_t* values, uint8_t count);

	// Agrega un array de booleanos desde memoria al objeto actual.
	bool objAppendBoolArray(const char* name, const bool* values, uint8_t count);

	// Agrega un valor string al array actual.
	bool arrAppendString(const char* value);

	// Agrega un valor booleano al array actual.
	bool arrAppendBool(bool value);

	// Agrega un valor entero con signo al array actual.
	bool arrAppendInt(int32_t value);

	// Agrega un valor entero sin signo al array actual.
	bool arrAppendUInt(uint32_t value);

	// Agrega un valor float al array actual.
	bool arrAppendFloat(double value, uint8_t decimals = 2);

	// Agrega un valor null al array actual.
	bool arrAppendNull();

	// Agrega un JsonWriter ya cerrado como valor del array actual.
	bool arrAppendJson(const JsonWriter& value);

	// Agrega JSON crudo como valor del array actual.
	bool arrAppendRaw(const char* rawJson);

	// Agrega un array de floats al array actual usando argumentos variables.
	bool arrAppendFloatArray(uint8_t count, uint8_t decimals, ...);

	// Agrega un array de enteros con signo al array actual usando argumentos variables.
	bool arrAppendIntArray(uint8_t count, ...);

	// Agrega un array de enteros sin signo al array actual usando argumentos variables.
	bool arrAppendUIntArray(uint8_t count, ...);

	// Agrega un array de booleanos al array actual usando argumentos variables.
	bool arrAppendBoolArray(uint8_t count, ...);

	// Agrega un array de strings al array actual usando argumentos variables.
	bool arrAppendStringArray(uint8_t count, ...);

private:
	enum class ContextType : uint8_t {
		Object,
		Array
	};

	struct Context {
		ContextType type;
		bool first = true;
	};

	static constexpr uint8_t MaxDepth = 8;

	char* buffer_ = nullptr;
	size_t capacity_ = 0;
	size_t length_ = 0;
	bool ownsBuffer_ = false;
	bool ok_ = true;
	Context stack_[MaxDepth] = {};
	uint8_t depth_ = 0;

	// Marca el writer como invalido.
	bool fail();

	// Agrega texto formateado usando una lista de argumentos ya iniciada.
	bool appendFormatArgs(const char* format, va_list args);

	// Devuelve true si hay un contexto abierto del tipo indicado.
	bool inContext(ContextType type) const;

	// Agrega separador antes de un nuevo valor.
	bool appendValuePrefix();

	// Agrega separador y nombre de campo antes de un valor de objeto.
	bool appendObjectFieldPrefix(const char* name);

	// Apila un nuevo contexto.
	bool pushContext(ContextType type);

	// Desapila el contexto indicado.
	bool popContext(ContextType type, char closeChar);

	// Agrega un array de floats leyendo valores desde va_list.
	bool appendFloatArrayValues(uint8_t count, uint8_t decimals, va_list args);

	// Agrega un array de enteros con signo leyendo valores desde va_list.
	bool appendIntArrayValues(uint8_t count, va_list args);

	// Agrega un array de enteros sin signo leyendo valores desde va_list.
	bool appendUIntArrayValues(uint8_t count, va_list args);

	// Agrega un array de booleanos leyendo valores desde va_list.
	bool appendBoolArrayValues(uint8_t count, va_list args);

	// Agrega un array de strings leyendo valores desde va_list.
	bool appendStringArrayValues(uint8_t count, va_list args);
};
