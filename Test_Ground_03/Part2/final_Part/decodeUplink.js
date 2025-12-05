function decodeUplink(input) {
  const b = input.bytes || [];
  const version = b[0] || 0;
  const event = b[1] === 1 ? 1 : 0; // 1 = SUCCESS, 0 = FAIL
  const user_id = (b[2] | (b[3]<<8) | (b[4]<<16) | (b[5]<<24)) >>> 0;

  return {
    data: {
      field1: user_id,
      field2: event,
      field3: version
    }
  };
}
