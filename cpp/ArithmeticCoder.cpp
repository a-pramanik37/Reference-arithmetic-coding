/* 
 * Reference arithmetic coding
 * Copyright (c) Project Nayuki
 * 
 * https://www.nayuki.io/page/reference-arithmetic-coding
 * https://github.com/nayuki/Reference-arithmetic-coding
 */

#include <limits>
#include <stdexcept>
#include "ArithmeticCoder.hpp"

using std::uint32_t;
using std::uint64_t;


ArithmeticCoderBase::ArithmeticCoderBase(int stateSize) {
	if (stateSize < 1 || stateSize > 63)
		throw std::domain_error("State size out of range");
	numStateBits = stateSize;
	fullRange = static_cast<decltype(fullRange)>(1) << numStateBits;
	halfRange = fullRange >> 1;
	quarterRange = halfRange >> 1;
	MIN_RANGE = (fullRange >> 2) + 2;
	MAX_TOTAL = std::min(std::numeric_limits<decltype(fullRange)>::max() / fullRange, MIN_RANGE);
	MASK = fullRange - 1;
	low = 0;
	high = MASK;
}


ArithmeticCoderBase::~ArithmeticCoderBase() {}


void ArithmeticCoderBase::update(const FrequencyTable &freqs, uint32_t symbol) {
	// State check
	if (low >= high || (low & MASK) != low || (high & MASK) != high)
		throw std::logic_error("Assertion error: Low or high out of range");
	uint64_t range = high - low + 1;
	if (range < MIN_RANGE || range > fullRange)
		throw std::logic_error("Assertion error: Range out of range");
	
	// Frequency table values check
	uint32_t total = freqs.getTotal();
	uint32_t symLow = freqs.getLow(symbol);
	uint32_t symHigh = freqs.getHigh(symbol);
	if (symLow == symHigh)
		throw std::invalid_argument("Symbol has zero frequency");
	if (total > MAX_TOTAL)
		throw std::invalid_argument("Cannot code symbol because total is too large");
	
	// Update range
	uint64_t newLow  = low + symLow  * range / total;
	uint64_t newHigh = low + symHigh * range / total - 1;
	low = newLow;
	high = newHigh;
	
	// While the highest bits are equal
	while (((low ^ high) & halfRange) == 0) {
		shift();
		low = (low << 1) & MASK;
		high = ((high << 1) & MASK) | 1;
	}
	
	// While the second highest bit of low is 1 and the second highest bit of high is 0
	while ((low & ~high & quarterRange) != 0) {
		underflow();
		low = (low << 1) & (MASK >> 1);
		high = ((high << 1) & (MASK >> 1)) | halfRange | 1;
	}
}


ArithmeticDecoder::ArithmeticDecoder(int stateSize, BitInputStream &in) :
		ArithmeticCoderBase(stateSize),
		input(in),
		code(0) {
	for (int i = 0; i < numStateBits; i++)
		code = code << 1 | readCodeBit();
}


uint32_t ArithmeticDecoder::read(const FrequencyTable &freqs) {
	// Translate from coding range scale to frequency table scale
	uint32_t total = freqs.getTotal();
	if (total > MAX_TOTAL)
		throw std::invalid_argument("Cannot decode symbol because total is too large");
	uint64_t range = high - low + 1;
	uint64_t offset = code - low;
	uint64_t value = ((offset + 1) * total - 1) / range;
	if (value * range / total > offset)
		throw std::logic_error("Assertion error");
	if (value >= total)
		throw std::logic_error("Assertion error");
	
	// A kind of binary search. Find highest symbol such that freqs.getLow(symbol) <= value.
	uint32_t start = 0;
	uint32_t end = freqs.getSymbolLimit();
	while (end - start > 1) {
		uint32_t middle = (start + end) >> 1;
		if (freqs.getLow(middle) > value)
			end = middle;
		else
			start = middle;
	}
	if (start + 1 != end)
		throw std::logic_error("Assertion error");
	
	uint32_t symbol = start;
	if (offset < freqs.getLow(symbol) * range / total || freqs.getHigh(symbol) * range / total <= offset)
		throw std::logic_error("Assertion error");
	update(freqs, symbol);
	if (code < low || code > high)
		throw std::logic_error("Assertion error: Code out of range");
	return symbol;
}


void ArithmeticDecoder::shift() {
	code = ((code << 1) & MASK) | readCodeBit();
}


void ArithmeticDecoder::underflow() {
	code = (code & halfRange) | ((code << 1) & (MASK >> 1)) | readCodeBit();
}


int ArithmeticDecoder::readCodeBit() {
	int temp = input.read();
	if (temp == -1)
		temp = 0;
	return temp;
}


ArithmeticEncoder::ArithmeticEncoder(int stateSize, BitOutputStream &out) :
	ArithmeticCoderBase(stateSize),
	output(out),
	numUnderflow(0) {}


void ArithmeticEncoder::write(const FrequencyTable &freqs, uint32_t symbol) {
	update(freqs, symbol);
}


void ArithmeticEncoder::finish() {
	output.write(1);
}


void ArithmeticEncoder::shift() {
	int bit = static_cast<int>(low >> (numStateBits - 1));
	output.write(bit);
	
	// Write out the saved underflow bits
	for (; numUnderflow > 0; numUnderflow--)
		output.write(bit ^ 1);
}


void ArithmeticEncoder::underflow() {
	if (numUnderflow == std::numeric_limits<decltype(numUnderflow)>::max())
		throw std::overflow_error("Maximum underflow reached");
	numUnderflow++;
}
