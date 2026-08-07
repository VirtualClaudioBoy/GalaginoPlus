# Protocollo Namco 58XX (da MAME namcoio.cpp, fetch 2026-07-12)

RAM interna 16 nibble (m_ram[16]). CPU legge/scrive nibble:
- read(offset)  -> 0xf0 | m_ram[offset & 0x0f]
- write(offset) -> m_ram[offset & 0x0f] = data & 0x0f

Ogni vblank (dopo ~50us in MAME) se non in reset: customio_run().
Comando = m_ram[8]. Porte input in_cb[0..3] (4 bit, attive alte lato
callback; il chip le inverte). Reset (mainlatch Q4=0): azzera ram.

## 58XX customio_run() — switch(ram[8])
- 0: nop
- 1: modo "switch inputs diretti":
    ram[4]=~in0, ram[5]=~in1, ram[6]=~in2, ram[7]=~in3
    out0(ram[9]), out1(ram[10])
- 2: set coinage: coins_per_cred[0]=ram[9], creds_per_coin[0]=ram[10],
    coins_per_cred[1]=ram[11], creds_per_coin[1]=ram[12]
- 3: credit mode: handle_coins(swap=2)
- 4: dipmux mode (usato da namcoio#1 per i DIP):
    out0(0); ram[0]=~in0, ram[2]=~in1, ram[4]=~in2, ram[6]=~in3
    out0(1); ram[1]=~in0, ram[3]=~in1, ram[5]=~in2, ram[7]=~in3
- 5: unica "protezione" PRNG (seed 0x22, NEXT(n)=((n&1)?n^0x90:n)>>1),
    scrive ram[1..7], ram[0]=0 (0xF se ram[9]==0xF)

## handle_coins(swap) — 58XX usa swap=2
```
val = ~in0 (pins 38-41: coin/service); toggled = val ^ lastcoins; lastcoins = val
coin1 edge (bit0): coins[0]++; se >= coins_per_cred[0]&7:
    credit_add = creds_per_coin[0] - (coins_per_cred[0]>>3); coins[0] -= cpc&7
    elif coins_per_cred[0]&8: credit_add=1
coin2 edge (bit1): idem con [1]
service edge (bit3): credit_add=1
val = ~in3 (pins 30-33: start1/start2 su bit2/bit3); toggled edge
se ram[9]==0 (start abilitati):
    start1 edge (bit2) e credits>=1: credit_sub=1
    elif start2 edge (bit3) e credits>=2: credit_sub=2
credits += credit_add - credit_sub
ram[0^swap] = credits/10 (BCD alto)   -> per 58XX: ram[2]
ram[1^swap] = credits%10 (BCD basso)  -> per 58XX: ram[3]
se credit_add: ram[2^swap]=credit_add -> ram[0]
se credit_sub: ram[3^swap]=credit_sub -> ram[1]
ram[4] = ~in1 (joystick P1)
ram[5] = ((val&0x05)<<1) | (val&toggled&0x05)   (bottoni: level<<1|edge)
ram[6] = ~in2 (joystick P2)
ram[7] = (val&0x0a) | ((val&toggled&0x0a)>>1)   (start: level|edge>>1)
```
NOTA: tutti i valori scritti in ram vanno mascherati a nibbla (&0x0f).

## Mappy (da mappy.cpp MAME)
- namcoio#0 (58XX @0x4800-0x480F): in0=COINS(4bit), in1=P1, in2=P2, in3=BUTTONS
- namcoio#1 (58XX @0x4810-0x481F): in0=dipmux via LS157 (out0 bit0 seleziona
  DSW2 lo/hi nibble), in1=DSW1 low, in2=DSW1 high, in3=DSW0
  -> gira in mode 4 (dipmux): out0(0)/out0(1) alterna la selezione LS157

## WSG 15XX (da namco.cpp) — regs 0x00-0x3F in RAM condivisa 0x4000-0x403F
Per canale ch (0..7), base = ch*8:
- +0x02: scrittura diretta bit alti counter (counter[19:15] = data&0x1f,
  modalita' DAC di Grobda; per mappy si puo' anche ignorare o supportare)
- +0x03: volume = data & 0x0f
- +0x04: freq bits 0-7
- +0x05: freq bits 8-15
- +0x06: bits 0-3 = freq bits 16-19; bits 4-6 = waveform select (0-7)
- freq 20 bit; a 24000Hz nativi galagino: counter 20.x bit fixed point,
  1 tick = 1 sample: counter += freq; pos = (counter>>15)&0x1f
  (counter con 15 bit frazionari => freq espressa cosi': out_freq_hz =
  freq * 24000 / 2^20 ... verificare col fixed point di galagino esistente)
- sample = wavetable[select][pos] (4 bit -8..+7) * volume
- sound_enable (mainlatch Q3): se 0, uscita muta
