package protocol

import "fmt"

// Encode applies COBS encoding to data, eliminating all 0x00 bytes.
// The output is always slightly larger than the input (at most 1 byte
// per 254 input bytes, plus 1). This mirrors froth_cobs_encode in
// froth_transport.c.
func COBSEncode(data []byte) []byte {
	// Algorithm:
	//
	// COBS groups runs of non-zero bytes. Each group is prefixed with
	// a "code byte" that tells the decoder how far to the next zero
	// (or end of block).
	//
	// 1. Allocate output slice. Worst case size: len(data) + len(data)/254 + 1.
	// 2. Reserve position 0 for the first code byte. Set writePos = 1.
	//    Set codePos = 0 (index of current code byte). Set code = 1.
	// 3. Walk each input byte:
	//    a. If byte == 0x00:
	//       - Write current code at out[codePos].
	//       - Set codePos = writePos. Advance writePos (reserve next code byte).
	//       - Reset code = 1.
	//    b. If byte != 0x00:
	//       - Write byte at out[writePos]. Advance writePos. Increment code.
	//       - If code reaches 0xFF (255):
	//         Write 0xFF at out[codePos]. Set codePos = writePos.
	//         Advance writePos. Reset code = 1.
	//         (0xFF means "254 data bytes follow, no implicit trailing zero")
	// 4. After loop: write final code at out[codePos].
	// 5. Return out[:writePos].

	out := make([]byte, len(data)+len(data)/254+2)
	wp := 1  // write position (0 reserved for first code)
	cp := 0  // code byte position
	code := byte(1)

	for _, b := range data {
		if b == 0 {
			out[cp] = code
			cp = wp
			wp++
			code = 1
		} else {
			out[wp] = b
			wp++
			code++
			if code == 0xFF {
				out[cp] = code
				cp = wp
				wp++
				code = 1
			}
		}
	}

	out[cp] = code
	return out[:wp]
}

// Decode reverses COBS encoding. Returns the decoded data or an error
// if the encoding is invalid. This mirrors froth_cobs_decode in
// froth_transport.c.
func COBSDecode(data []byte) ([]byte, error) {
	// Algorithm:
	//
	// 1. Allocate output slice (decoded is always <= encoded length).
	// 2. Set readPos = 0, writePos = 0.
	// 3. While readPos < len(data):
	//    a. Read code byte from data[readPos]. Advance readPos.
	//    b. If code == 0: invalid encoding, return error.
	//    c. Copy (code - 1) bytes from data[readPos..] to output.
	//       Advance readPos by (code - 1).
	//    d. If code < 0xFF AND readPos < len(data):
	//       append a 0x00 byte to output (implicit zero).
	// 4. Return output.

	out := make([]byte, 0, len(data))
	rp := 0

	for rp < len(data) {
		code := data[rp]
		rp++

		if code == 0 {
			return nil, fmt.Errorf("cobs: zero byte in encoded data")
		}

		count := int(code) - 1
		if rp+count > len(data) {
			return nil, fmt.Errorf("cobs: truncated block (need %d bytes, have %d)", count, len(data)-rp)
		}

		out = append(out, data[rp:rp+count]...)
		rp += count

		if code < 0xFF && rp < len(data) {
			out = append(out, 0x00)
		}
	}

	return out, nil
}
