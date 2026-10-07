"""Validate the selected build profile against a Minecraft ELF binary."""
import mmap
import re
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
profile = (root / 'minecraft_build.h').read_text()

def integer(name):
    return int(re.search(r'\b' + name + r' = (0x[0-9a-f]+|[0-9]+);', profile)[1], 0)

def array(name):
    body = re.search(r'\b' + name + r'\[\] = \{(.*?)\};', profile, re.S)[1]
    return bytes(int(value, 0) for value in re.findall(r'0x[0-9a-f]+|(?<![\w])\d+', body))

version = re.search(r'version\[\] = "([^"]+)"', profile)[1]
path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / '.local/share/mcpelauncher/versions' / version / 'lib/x86_64/libminecraftpe.so'
with path.open('rb') as file:
    image = mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ)
sections_offset = struct.unpack_from('<Q', image, 40)[0]
count = struct.unpack_from('<H', image, 60)[0]
sections = [struct.unpack_from('<IIQQQQIIQQ', image, sections_offset + i * 64) for i in range(count)]

def read(address, size):
    for section in sections:
        if section[1] != 8 and section[3] <= address and address + size <= section[3] + section[5]:
            offset = section[4] + address - section[3]
            return image[offset:offset + size]
    raise AssertionError(f'Unmapped address {address:#x}')

relocations = {}
for section in sections:
    if section[1] == 4:
        for offset in range(section[4], section[4] + section[5], 24):
            address, info, value = struct.unpack_from('<QQq', image, offset)
            if info & 0xffffffff == 8:
                relocations[address] = value

build_note = integer('buildNote')
build_id = array('buildId')
assert read(build_note, 36) == struct.pack('<III4s', 4, 20, 3, b'GNU\0') + build_id
assert relocations[integer('dispatcherSlot')] == integer('dispatchFunction')
assert read(integer('dispatchFunction'), integer('dispatchSignatureSize')) == array('dispatchSignature')
for table in ('handlerVtable', 'legacyHandlerVtable'):
    assert relocations[integer(table) + integer('textHandlerSlot')] == integer('handleTextFunction')
# Approved Lobby Scanner sends borrow handlers from these three typed dispatchers.
def addresses(name):
    return [int(value, 0) for value in re.findall(r'0x[0-9a-f]+|(?<![\w])\d+',
        re.search(rf'{name}\[\] = \{{(.*?)\}}', profile, re.S)[1])]
for slot, function, handler_slot, base_handler, legacy_handler in zip(
        addresses('lobbyDispatchSlots'), addresses('lobbyDispatchFunctions'), addresses('lobbyHandlerSlots'),
        addresses('lobbyBaseHandlers'), addresses('lobbyLegacyHandlers')):
    assert relocations[slot] == function
    signature = bytes.fromhex('4889d7488b11488b07488b80') + struct.pack('<I', handler_slot) + bytes.fromhex('ffe0')
    assert read(function, len(signature)) == signature
    assert relocations[integer('handlerVtable') + handler_slot] == base_handler
    assert relocations[integer('legacyHandlerVtable') + handler_slot] == legacy_handler
for slot, getter in (('clientPlayerSlot', 'clientPlayerGetter'), ('clientXuidSlot', 'clientXuidGetter'),
                     ('clientIdentitySlot', 'clientIdentityGetter'), ('clientSenderSlot', 'clientSenderGetter'),
                     ('clientGameGetterSlot', 'clientGameGetter')):
    assert relocations[integer('clientVtable') + integer(slot)] == integer(getter)
chat = {name: integer(name) for name in (
    'nativeSubmitAuthor', 'nativeSubmitIdentity', 'nativeSubmitXuid', 'nativeSubmitSender',
    'nativeSubmitLocalEcho', 'nativeSubmitLevelEcho')}
for address, opcode in ((chat['nativeSubmitAuthor'], '488b90100b0000'),
                        (chat['nativeSubmitIdentity'], 'ff9088040000'),
                        (chat['nativeSubmitXuid'], 'ff90a0050000'),
                        (chat['nativeSubmitSender'], 'ff9030090000'),
                        (chat['nativeSubmitLocalEcho'], 'ff91b0060000'),
                        (chat['nativeSubmitLevelEcho'], 'ff9148070000')):
    assert read(address, len(bytes.fromhex(opcode))) == bytes.fromhex(opcode)
constructor_call = integer('textPacketConstructorCall')
constructor_offset = constructor_call + 5 + struct.unpack('<i', read(constructor_call + 1, 4))[0]
assert constructor_offset == integer('constructor')
for address, signature in (('commandPacketConstructor', 'commandPacketConstructorSignature'),
                           ('nativeStringConstructor', 'nativeStringConstructorSignature'),
                           ('commandPacketGetId', 'commandPacketGetIdSignature')):
    assert read(integer(address), len(array(signature))) == array(signature)
assert relocations[integer('commandPacketVtable') + 0x10] == integer('commandPacketGetId')
zoom_sites = [int(value, 0) for value in re.findall(r'0x[0-9a-f]+|(?<![\w])\d+',
    re.search(r'sites\[\] = \{(.*?)\}', profile, re.S)[1])]
for site in zoom_sites:
    assert read(site, len(array('cameraCall'))) == array('cameraCall')
assert read(zoom_sites[0] - integer('firstSetup'), len(array('firstRead'))) == array('firstRead')
floor = integer('floorSite')
assert read(floor, len(array('floorLoad'))) == array('floorLoad')
floor_target = floor + len(array('floorLoad')) + struct.unpack('<i', read(floor + 4, 4))[0]
assert floor_target == integer('floorLoadTarget') and struct.unpack('<f', read(floor_target, 4))[0] == 5
print(f'PASS: Minecraft {version} matches the centralized AutoGG and Zoom profile')

assert relocations[integer('listVtable') + integer('listInvokeSlot')] == integer('listCallback')
for address, signature in (('listCallback', 'listEntry'), ('listCaptureSite', 'listCaptureSignature'),
                           ('listOutputSelect', 'listOutputSignature'),
                           ('listConsumptionSite', 'listConsumptionSignature')):
    assert read(integer(address), len(array(signature))) == array(signature)
print('PASS: Render callback slot, closure captures, output selection, and terrain-list consumption')

for address, signature in (('prepareFunction', 'prepareFunctionSignature'),
                           ('prepareCall', 'prepareCallSignature'),
                           ('prepareArguments', 'prepareArgumentsSignature')):
    assert read(integer(address), len(array(signature))) == array(signature)
prepare_call = integer('prepareCall')
assert prepare_call + 5 + struct.unpack('<i', read(prepare_call + 1, 4))[0] == integer('prepareFunction')
gl_profile = re.search(r'namespace glTrace \{(.*?)\n\}\n', profile, re.S)[1]
gl_slots = [int(value, 16) for value in re.findall(r'0x[0-9a-f]+', re.search(r'slots\[\] = \{(.*?)\}', gl_profile)[1])]
gl_plt = [int(value, 16) for value in re.findall(r'0x[0-9a-f]+', re.search(r'plt\[\] = \{(.*?)\}', gl_profile)[1])]
gl_signatures = [bytes(int(value, 16) for value in re.findall(r'0x[0-9a-f]+', body))
                 for body in re.findall(r'\{([^{}]+)\}', re.search(r'pltSignatures\[4\]\[6\] = \{(.*?)\};', gl_profile, re.S)[1])]
assert len(gl_slots) == len(gl_plt) == len(gl_signatures) == 4
for slot, plt, signature in zip(gl_slots, gl_plt, gl_signatures):
    assert read(plt, 6) == signature
    assert plt + 6 + struct.unpack('<i', signature[2:])[0] == slot
# Confirm jump-slot relocation symbols, rather than treating GOT offsets as names.
expected_imports = dict(zip(gl_slots, ('glDrawArrays', 'glDrawElements', 'glBufferData', 'glBufferSubData')))
found_imports = {}
for section in sections:
    if section[1] != 4: continue
    symbols = sections[section[6]]
    strings = sections[symbols[6]]
    for offset in range(section[4], section[4] + section[5], 24):
        address, info, _ = struct.unpack_from('<QQq', image, offset)
        if address not in expected_imports: continue
        assert info & 0xffffffff == 7  # R_X86_64_JUMP_SLOT
        symbol = symbols[4] + (info >> 32) * symbols[9]
        name_offset = struct.unpack_from('<I', image, symbol)[0] + strings[4]
        found_imports[address] = image[name_offset:image.find(b'\0', name_offset)].decode()
assert found_imports == expected_imports
print('PASS: Terrain preparation caller/argument ABI and GL diagnostic import symbols/PLT slots')

# Shader program lifecycle imports used by the sky service.
program_profile = re.search(r'namespace shaderPrograms \{(.*?)\n\}\n', profile, re.S)[1]
def program_addresses(name):
    return [int(value, 16) for value in re.findall(r'0x[0-9a-f]+',
        re.search(rf'{name}\[3\] = \{{(.*?)\}}', program_profile)[1])]
program_signatures = [bytes(int(value, 16) for value in re.findall(r'0x[0-9a-f]+', body))
    for body in re.findall(r'\{([^{}]+)\}',
        re.search(r'pltSignatures\[3\]\[6\] = \{(.*?)\};', program_profile, re.S)[1])]
for slot, plt, signature in zip(program_addresses('slots'), program_addresses('plt'), program_signatures):
    assert read(plt, 6) == signature and plt+6+struct.unpack('<i', signature[2:])[0] == slot
program_imports = dict(zip(program_addresses('slots'), ('glLinkProgram', 'glDeleteProgram', 'glUseProgram')))
for section in sections:
    if section[1] != 4: continue
    symbols = sections[section[6]]; strings = sections[symbols[6]]
    for offset in range(section[4], section[4]+section[5], 24):
        address, info, _ = struct.unpack_from('<QQq', image, offset)
        if address not in program_imports: continue
        assert info & 0xffffffff == 7
        symbol = symbols[4]+(info >> 32)*symbols[9]
        name_offset = struct.unpack_from('<I', image, symbol)[0]+strings[4]
        assert image[name_offset:image.find(b'\0', name_offset)].decode() == program_imports.pop(address)
assert not program_imports
print('PASS: Shader program import symbols, PLT gates and lifecycle imports')

# Sky source/compile and instanced/indirect draw lookup use their own gated imports.
sky_imports = {}
for namespace, symbol in (('shaderSource', 'glShaderSource'), ('shaderCompile', 'glCompileShader'),
                          ('drawProc', 'eglGetProcAddress')):
    scope = re.search(r'namespace ' + namespace + r' \{(.*?)\n\}', profile, re.S)[1]
    slot = int(re.search(r' slot = (0x[0-9a-f]+);', scope)[1], 16)
    plt = int(re.search(r' plt = (0x[0-9a-f]+);', scope)[1], 16)
    signature = bytes(int(value, 16) for value in re.findall(r'0x[0-9a-f]+',
        re.search(r'pltSignature\[\] = \{(.*?)\}', scope)[1]))
    assert read(plt, len(signature)) == signature
    assert plt + 6 + struct.unpack('<i', signature[2:])[0] == slot
    sky_imports[slot] = symbol
for section in sections:
    if section[1] != 4: continue
    symbols = sections[section[6]]; strings = sections[symbols[6]]
    for offset in range(section[4], section[4]+section[5], 24):
        address, info, _ = struct.unpack_from('<QQq', image, offset)
        if address not in sky_imports: continue
        assert info & 0xffffffff == 7
        name_offset = struct.unpack_from('<I', image, symbols[4]+(info >> 32)*symbols[9])[0]+strings[4]
        assert image[name_offset:image.find(b'\0', name_offset)].decode() == sky_imports.pop(address)
assert not sky_imports
print('PASS: Sky shader source/compile and EGL draw-procedure import gates')

# Native GL submission entry/ABI and presentation import.
assert relocations[integer('submitSlot')] == integer('submitFunction')
assert integer('submitSlot') == integer('submitVtable')+integer('submitInvokeOffset')
assert integer('submitCtor')+7+struct.unpack('<i',read(integer('submitCtor')+3,4))[0] == integer('submitVtable')
assert struct.unpack('<I',array('submitCallerSignature')[-4:])[0] == integer('submitInvokeOffset')
for address, signature in (('submitFunction','submitSignature'), ('submitCaller','submitCallerSignature'),
                           ('submitCtor','submitCtorSignature'), ('submitReturn','submitReturnSignature'),
                           ('swapPlt','swapSignature')):
    assert read(integer(address),len(array(signature))) == array(signature)
assert integer('swapPlt')+6+struct.unpack('<i',array('swapSignature')[2:])[0] == integer('swapSlot')
found_swap=False
swap_slot=integer('swapSlot')
for section in sections:
    if section[1] != 4: continue
    symbols=sections[section[6]]; strings=sections[symbols[6]]
    for offset in range(section[4],section[4]+section[5],24):
        address,info,_=struct.unpack_from('<QQq',image,offset)
        if address!=swap_slot: continue
        assert info & 0xffffffff == 7
        name_offset=struct.unpack_from('<I',image,symbols[4]+(info >> 32)*symbols[9])[0]+strings[4]
        assert image[name_offset:image.find(b'\0',name_offset)].decode() == 'eglSwapBuffers'
        found_swap=True
assert found_swap
print('PASS: Native GL submit vtable/entry/caller ABI and EGL presentation import')

# Player roster, skin updates, and world lifecycle dispatchers.
def integers(name):
    body = re.search(r'\b' + name + r'\[\] = \{(.*?)\};', profile, re.S)[1]
    return [int(value, 0) for value in re.findall(r'0x[0-9a-f]+|(?<![\w])\d+', body)]
for slot, function, handler_slot, base_handler, legacy_handler in zip(
        integers('rosterSlots'), integers('rosterFunctions'), integers('rosterHandlerSlots'),
        integers('rosterBaseHandlers'), integers('rosterLegacyHandlers')):
    assert relocations[slot] == function
    assert read(function, 18) == bytes.fromhex('4889d7488b11488b07488b80') + struct.pack('<I', handler_slot) + bytes.fromhex('ffe0')
    assert relocations[integer('handlerVtable') + handler_slot] == base_handler
    assert relocations[integer('legacyHandlerVtable') + handler_slot] == legacy_handler
for name in ('rosterAddLayout', 'rosterRemoveLayout', 'rosterVectorLayout', 'skinPacketLayout',
             'rosterVariantLayout', 'skinImageFormat', 'skinImageData', 'skinImageWidth', 'skinImageHeight', 'skinImageInit'):
    assert read(integer(name + 'Site'), len(array(name + 'Signature'))) == array(name + 'Signature')
print('PASS: Tablist dispatcher, handler, roster/skin ABI, and world lifecycle')

# Particles uses the native entity attack and LocalPlayer critical emitter.
for table, function in zip(integers('attackTables'), integers('attackFunctions')):
    assert relocations[table + integer('attackSlot')] == function
for address, signature in ((integers('attackFunctions')[0], 'attackEntry'),
                           (integers('attackFunctions')[1], 'survivalAttackEntry'),
                           (integer('attackPlayerSite'), 'attackPlayerRead'),
                           (integer('criticalEmitter'), 'criticalEntry'),
                           (integer('emitterBody'), 'emitterEntry')):
    assert read(address, len(array(signature))) == array(signature)
assert array('attackPlayerRead')[3] == integer('modePlayer')
assert relocations[integer('localPlayerTable') + integer('criticalSlot')] == integer('criticalEmitter')
critical = integer('criticalEmitter')
name = critical + 17 + struct.unpack('<i', read(critical + 13, 4))[0]
assert read(name, 31) == b'minecraft:critical_hit_emitter\0'
assert critical + 27 + struct.unpack('<i', read(critical + 23, 4))[0] == integer('emitterBody')
assert read(critical + 6, 4) == struct.pack('<I', integer('localParticleContext'))
# Confirm exact runtime vtable identities through their RTTI names.
for table, expected in (('localPlayerTable', b'11LocalPlayer\0'),
                        ('remotePlayerTable', b'12RemotePlayer\0')):
    info = relocations[integer(table) - 8]
    assert read(relocations[info + 8], len(expected)) == expected
print('PASS: Particles attack slots/thunks, LocalPlayer/RemotePlayer RTTI, and native critical-hit emitter')

for table, function in zip(addresses('fogTables'), addresses('fogFunctions')):
    assert relocations[table + integer('fogSlot')] == function
assert relocations[addresses('fogTables')[0] + integer('angleSlot')] == integer('angleFunction')
for name in ('fogOverworld', 'fogPassthrough', 'angleEntry', 'fogCaller', 'angleCaller'):
    assert read(integer(name + 'Site'), len(array(name + 'Signature'))) == array(name + 'Signature')
print('PASS: Environment Dimension slots, color return ABI, celestial-angle ticks, and native callers')

# Read-only player targeting uses native camera unprojection and Level iteration.
for name in ('cameraGetter', 'matrixMultiply', 'matrixInverse', 'forEachPlayer',
             'playerIteration', 'playerIterationCall', 'shapeRead', 'cameraPickRead',
             'cameraMatrices', 'playerNameRead'):
    assert read(integer(name), len(array(name + 'Signature'))) == array(name + 'Signature')
assert relocations[integer('clientLevelTable') + integer('forEachPlayerSlot')] == integer('forEachPlayer')
info = relocations[integer('clientLevelTable') - 8]
assert read(relocations[info + 8], 14) == b'11ClientLevel\0'
assert read(integer('playerNameRead'), 7) == bytes.fromhex('f680') + struct.pack('<I', integer('actorName')) + b'\x01'
assert read(integer('shapeRead'), 7) == bytes.fromhex('488b87') + struct.pack('<I', integer('actorShape'))
print('PASS: Player targeting camera/matrix ABI, Level player iteration, Actor AABB and name reads')
