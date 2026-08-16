"""65816 SSA IR over the byte-exact DKC1 listing.

Staged per docs/SSA_IR_DESIGN.md; every stage carries its own validation
gate (tools/ir_validate.py) and nothing downstream consumes a stage that
has not passed. The IR never severs the 1:1 link to exact assembly: each
op keeps its verbatim operand expression, source row, and 24-bit address.
"""
