# 
# Decompression application using prediction by partial matching (PPM) with arithmetic coding
# 
# Usage: python ppm-decompress.py InputFile OutputFile
# This decompresses files generated by the ppm-compress.py application.
# 
# Copyright (c) Project Nayuki
# 
# https://www.nayuki.io/page/reference-arithmetic-coding
# https://github.com/nayuki/Reference-arithmetic-coding
# 

import sys
import arithmeticcoding, ppmmodel
python3 = sys.version_info.major >= 3


# Must be at least -1 and match ppm-compress.py. Warning: Exponential memory usage at O(257^n).
MODEL_ORDER = 3


# Command line main application function.
def main(args):
	# Handle command line arguments
	if len(args) != 2:
		sys.exit("Usage: python ppm-decompress.py InputFile OutputFile")
	inputfile  = args[0]
	outputfile = args[1]
	
	# Perform file decompression
	with open(inputfile, "rb") as inp, open(outputfile, "wb") as out:
		bitin = arithmeticcoding.BitInputStream(inp)
		decompress(bitin, out)


def decompress(bitin, out):
	# Set up decoder and model
	dec = arithmeticcoding.ArithmeticDecoder(bitin)
	model = ppmmodel.PpmModel(MODEL_ORDER, 257, 256)
	history = []
	
	while True:
		# Decode and write one byte
		symbol = decode_symbol(dec, model, history)
		if symbol == 256:  # EOF symbol
			break
		out.write(bytes((symbol,)) if python3 else chr(symbol))
		model.increment_contexts(history, symbol)
		
		if model.model_order >= 1:
			# Append current symbol or shift back by one
			if len(history) == model.model_order:
				del history[0]
			history.append(symbol)


def decode_symbol(dec, model, history):
	for order in reversed(range(min(len(history), model.model_order) + 1)):
		ctx = model.root_context
		# Note: We can't simplify the slice start to just '-order' because order can be 0
		for sym in history[len(history) - order : ]:
			assert ctx.subcontexts is not None
			ctx = ctx.subcontexts[sym]
			if ctx is None:
				break
		else:  # ctx is not None
			symbol = dec.read(ctx.frequencies)
			if symbol < 256:
				return symbol
			# Else we read the context escape symbol, so continue decrementing the order
	# Logic for order = -1
	return dec.read(model.order_minus1_freqs)


# Main launcher
if __name__ == "__main__":
	main(sys.argv[1 : ])
