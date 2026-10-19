#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <esp_system.h>

namespace {

constexpr uint8_t rolePin = 1;
constexpr uint8_t radioCePin = 10;
constexpr uint8_t radioCsnPin = 7;
constexpr uint8_t radioSckPin = 4;
constexpr uint8_t radioMisoPin = 5;
constexpr uint8_t radioMosiPin = 6;
constexpr uint8_t uartRxPin = 20;
constexpr uint8_t uartTxPin = 21;
constexpr uint32_t serialBaud = 115200;
constexpr uint8_t radioChannel = 80;
constexpr size_t packetDataSize = 25;
constexpr size_t inputBufferSize = 2048;
constexpr size_t outputBufferSize = 4096;
constexpr uint32_t packetFlushDelayUs = 2500;

constexpr uint8_t usbSource = 1;
constexpr uint8_t uartSource = 2;

const uint8_t radioAddress[5] = {'P', 'M', '9', '0', '0'};

struct __attribute__((packed)) radioPacket {
	uint8_t nodeId;
	uint16_t sessionId;
	uint16_t sequence;
	uint8_t source;
	uint8_t length;
	uint8_t data[packetDataSize];
};

static_assert(sizeof(radioPacket) == 32, "NRF24 payload must be 32 bytes");

// Guarda bytes en una cola circular fija para no depender del heap.
template <size_t capacity>
class byteRing {
public:
	// Agrega value al final; devuelve false si la cola esta llena.
	bool push(uint8_t value) {
		if (itemCount == capacity) return false;
		data[headIndex] = value;
		headIndex = (headIndex + 1) % capacity;
		++itemCount;
		return true;
	}

	// Saca el byte mas viejo en value; devuelve false si la cola esta vacia.
	bool pop(uint8_t &value) {
		if (itemCount == 0) return false;
		value = data[tailIndex];
		tailIndex = (tailIndex + 1) % capacity;
		--itemCount;
		return true;
	}

	// Devuelve la cantidad de bytes pendientes.
	size_t size() const { return itemCount; }

private:
	uint8_t data[capacity]{};
	size_t headIndex = 0;
	size_t tailIndex = 0;
	size_t itemCount = 0;
};

RF24 radio(radioCePin, radioCsnPin);
byteRing<inputBufferSize> usbInput;
byteRing<inputBufferSize> uartInput;
byteRing<outputBufferSize> usbOutput;
byteRing<outputBufferSize> uartOutput;

radioPacket pendingPacket{};
bool radioReady = false;
bool pendingPacketReady = false;
bool preferUsbInput = false;
bool remoteSequenceValid = false;
uint8_t localNodeId = 0;
uint8_t remoteNodeId = 0;
uint16_t localSessionId = 0;
uint16_t nextSequence = 0;
uint16_t remoteSessionId = 0;
uint16_t lastRemoteSequence = 0;
uint32_t usbFirstByteUs = 0;
uint32_t uartFirstByteUs = 0;
uint32_t retryAtUs = 0;
uint32_t inputOverflowCount = 0;
uint32_t outputOverflowCount = 0;
uint32_t droppedPacketCount = 0;

// Lee port, encola sus bytes en queue y actualiza firstByteUs al iniciar la cola.
void captureInput(Stream &port, byteRing<inputBufferSize> &queue,
				 uint32_t &firstByteUs) {
	while (port.available() > 0) {
		const int value = port.read();
		if (value < 0) break;
		if (queue.size() == 0) firstByteUs = micros();
		if (!queue.push(static_cast<uint8_t>(value))) ++inputOverflowCount;
	}
}

// Entrega los bytes en queue al USB mientras el controlador tenga espacio.
void flushUsbOutput() {
	while (usbOutput.size() > 0 && Serial.availableForWrite() > 0) {
		uint8_t value;
		if (!usbOutput.pop(value)) break;
		Serial.write(value);
	}
}

// Entrega los bytes en queue al UART mientras el hardware tenga espacio.
void flushUartOutput() {
	while (uartOutput.size() > 0 && Serial1.availableForWrite() > 0) {
		uint8_t value;
		if (!uartOutput.pop(value)) break;
		Serial1.write(value);
	}
}

// Encola el paquete recibido para copiarlo a USB y UART.
void queueRadioOutput(const radioPacket &packet) {
	for (uint8_t i = 0; i < packet.length; ++i) {
		if (!usbOutput.push(packet.data[i])) ++outputOverflowCount;
		if (!uartOutput.push(packet.data[i])) ++outputOverflowCount;
	}
}

// Rechaza paquetes duplicados y reconoce un nuevo arranque del nodo remoto.
bool acceptSequence(const radioPacket &packet) {
	if (packet.sessionId != remoteSessionId) {
		remoteSessionId = packet.sessionId;
		remoteSequenceValid = false;
	}
	if (remoteSequenceValid) {
		const uint16_t distance = packet.sequence - lastRemoteSequence;
		if (distance == 0 || distance >= 0x8000) {
			++droppedPacketCount;
			return false;
		}
		if (distance > 1) droppedPacketCount += distance - 1;
	}
	lastRemoteSequence = packet.sequence;
	remoteSequenceValid = true;
	return true;
}

// Lee los paquetes RF pendientes y los copia a las dos salidas locales.
void receiveRadioPackets() {
	while (radio.available()) {
		radioPacket packet{};
		radio.read(&packet, sizeof(packet));
		if (packet.nodeId != remoteNodeId || packet.length > packetDataSize) continue;
		if (packet.source != usbSource && packet.source != uartSource) continue;
		if (acceptSequence(packet)) queueRadioOutput(packet);
	}
}

// Indica si queue tiene packetDataSize bytes o vencio la espera desde firstByteUs.
bool packetReady(const byteRing<inputBufferSize> &queue, uint32_t firstByteUs) {
	return queue.size() >= packetDataSize ||
		(queue.size() > 0 && static_cast<uint32_t>(micros() - firstByteUs) >= packetFlushDelayUs);
}

// Prepara el siguiente paquete listo alternando entre USB y UART.
bool prepareRadioPacket() {
	const bool usbReady = packetReady(usbInput, usbFirstByteUs);
	const bool uartReady = packetReady(uartInput, uartFirstByteUs);
	if (!usbReady && !uartReady) return false;

	const bool useUsb = usbReady && (!uartReady || preferUsbInput);
	byteRing<inputBufferSize> &queue = useUsb ? usbInput : uartInput;
	uint32_t &firstByteUs = useUsb ? usbFirstByteUs : uartFirstByteUs;
	pendingPacket = {};
	pendingPacket.nodeId = localNodeId;
	pendingPacket.sessionId = localSessionId;
	pendingPacket.sequence = nextSequence;
	pendingPacket.source = useUsb ? usbSource : uartSource;
	pendingPacket.length = static_cast<uint8_t>(min(queue.size(), packetDataSize));
	for (uint8_t i = 0; i < pendingPacket.length; ++i) queue.pop(pendingPacket.data[i]);
	if (queue.size() == 0) firstByteUs = 0;
	pendingPacketReady = true;
	preferUsbInput = !useUsb;
	return true;
}

// Envia el paquete pendiente y conserva su secuencia si necesita reintento.
void transmitRadioPacket() {
	if (!radioReady || !pendingPacketReady) return;
	if (static_cast<int32_t>(micros() - retryAtUs) < 0) return;

	radio.stopListening();
	const bool sent = radio.write(&pendingPacket, sizeof(pendingPacket));
	radio.startListening();
	if (sent) {
		++nextSequence;
		pendingPacketReady = false;
		retryAtUs = micros();
	} else {
		retryAtUs = micros() + 3000 + (esp_random() % 5000);
	}
}

// Si el radio no inicia, conecta localmente USB y UART para permitir pruebas.
void forwardLocally() {
	uint8_t value;
	while (usbInput.pop(value)) {
		if (!uartOutput.push(value)) ++outputOverflowCount;
	}
	while (uartInput.pop(value)) {
		if (!usbOutput.push(value)) ++outputOverflowCount;
	}
}

} // namespace

// Inicializa USB, UART, el rol del nodo y el enlace NRF24.
void setup() {
	Serial.begin(serialBaud);
	Serial1.setRxBufferSize(inputBufferSize);
	Serial1.setTxBufferSize(inputBufferSize);
	Serial1.begin(serialBaud, SERIAL_8N1, uartRxPin, uartTxPin);

	pinMode(rolePin, INPUT_PULLUP);
	localNodeId = digitalRead(rolePin) == LOW ? 1 : 2;
	remoteNodeId = localNodeId == 1 ? 2 : 1;
	localSessionId = static_cast<uint16_t>(esp_random());
	if (localSessionId == 0) localSessionId = 1;
	randomSeed(esp_random());

	SPI.begin(radioSckPin, radioMisoPin, radioMosiPin, radioCsnPin);
	radioReady = radio.begin(&SPI);
	if (!radioReady) return;

	radio.setChannel(radioChannel);
	radio.setDataRate(RF24_1MBPS);
	radio.setPALevel(RF24_PA_LOW);
	radio.setCRCLength(RF24_CRC_16);
	radio.setAutoAck(true);
	radio.setRetries(2, 5);
	radio.disableDynamicPayloads();
	radio.setPayloadSize(sizeof(radioPacket));
	radio.openWritingPipe(radioAddress);
	radio.openReadingPipe(1, radioAddress);
	radio.startListening();
}

// Mueve los datos entre USB, UART y RF sin bloquear las entradas.
void loop() {
	captureInput(Serial, usbInput, usbFirstByteUs);
	captureInput(Serial1, uartInput, uartFirstByteUs);

	if (radioReady) {
		receiveRadioPackets();
		if (!pendingPacketReady) prepareRadioPacket();
		transmitRadioPacket();
	} else {
		forwardLocally();
	}

	flushUsbOutput();
	flushUartOutput();
}
