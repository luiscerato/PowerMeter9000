#pragma once

#include <atomic>

// Cuenta una llamada activa y la rechaza si el Store esta pausado por formato.
class StoreFormatGuard {
public:
	// Entra al Store mientras paused permanezca falso; activeCalls cuenta llamadas concurrentes.
	StoreFormatGuard(std::atomic<bool>& paused, std::atomic<uint16_t>& activeCalls)
		: pausedState(paused), activeCount(activeCalls)
	{
		if (pausedState.load()) return;
		activeCount.fetch_add(1);
		if (pausedState.load()) activeCount.fetch_sub(1);
		else entered = true;
	}

	// Libera el contador solo si el Store acepto esta llamada.
	~StoreFormatGuard()
	{
		if (entered) activeCount.fetch_sub(1);
	}

	// Indica si la llamada puede acceder al Store.
	explicit operator bool() const { return entered; }

private:
	std::atomic<bool>& pausedState;
	std::atomic<uint16_t>& activeCount;
	bool entered = false;
};
