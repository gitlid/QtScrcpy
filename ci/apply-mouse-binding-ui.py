"""Integrate mouse fields into the exact v2 editor baseline, once."""
from pathlib import Path
import hashlib
root = Path(__file__).resolve().parents[1]
expected = {
    'QtScrcpy/ui/keymapeditor.cpp': '52c78d6a1d6eb63615b2b483aae314f6f131201efbe9efd66840a17397f9d930',
    'QtScrcpy/ui/keymapdocument.h': 'a63a599330441ee2307a2805a5e83b6079054d46362c2d9772b7b60ec3d7a57b',
    'QtScrcpy/ui/videoform.cpp': 'cbc44a50b8d1cb0a04d1b3282aec2457d738979ad7f7f0f67aae08ed6b740633',
    'tests/CMakeLists.txt': '077ca57a9df7c15cb94a9591d7588d7a4d7b3d4545310c8e12680d947a7c9965',
}
if '#include "inputbindingfield.h"' in (root / 'QtScrcpy/ui/keymapeditor.cpp').read_text():
    print('Mouse editor integration already applied.')
    raise SystemExit(0)
for name, digest in expected.items():
    if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
        raise SystemExit(f'Unexpected baseline: {name}; refusing to overwrite')
p = root / 'QtScrcpy/ui/keymapeditor.cpp'
s = p.read_text()
a = s.index('class KeyField:')
b = s.index('class Marker:', a)
s = s[:a] + s[b:]
s = s.replace('#include "keymapeditor.h"', '#include "keymapeditor.h"\n#include "inputbindingfield.h"')
s = s.replace('new KeyField(this)', 'new InputBindingField(this)')
s = s.replace('m_keys.append(k);form->addRow', 'k->setObjectName(QString("mappingBinding%1").arg(i));m_keys.append(k);form->addRow')
s = s.replace('可视化按键设置 — 点击、WASD、滑动', '可视化键鼠设置 — 点击、WASD、滑动')
s = s.replace('点击输入框后按键绑定；拖动画面上的圆点调整位置。滑动有起点和终点。', '绑定框右侧箭头可选鼠标左键、右键、中键和侧键 X1 / X2；也可在框内按键或按侧键绑定。\\n单击绑定框只选中，不绑定左键。拖动画面圆点调整位置；滑动有起点和终点。')
s = s.replace('n["type"].toString()+"  "+n["key"].toString()', 'n["type"].toString()+"  "+InputBinding::label(n["key"].toString())')
s = s.replace('n["key"].toString().remove("Key_")', 'InputBinding::label(n["key"].toString()).remove("Key_")')
p.write_text(s)
p = root / 'QtScrcpy/ui/keymapdocument.h'
s = p.read_text().replace('#include <cmath>', '#include <cmath>\n#include "inputbinding.h"')
a = s.index(' static bool binding(')
b = s.index('\n', a)
s = s[:a] + ' static bool binding(const QString&s){return !InputBinding::identity(s).isEmpty();}' + s[b:]
s = s.replace('QSet<QString>keys{o["switchKey"].toString()};', 'QSet<QString>keys{InputBinding::identity(o["switchKey"].toString())};')
s = s.replace('QString s=v.toString();if(!v.isString()||!binding(s)||keys.contains(s))return false;keys.insert(s);', 'QString s=InputBinding::identity(v.toString());if(!v.isString()||s.isEmpty()||keys.contains(s))return false;keys.insert(s);')
p.write_text(s)
p = root / 'QtScrcpy/ui/videoform.cpp'
s = p.read_text().replace('#include "videoform.h"', '#include "videoform.h"\n#include "inputbinding.h"')
s = s.replace('if (device && !device->isCurrentCustomKeymap()) {', 'if (device && !device->isCurrentCustomKeymap()\n            && !InputBinding::isMouseSwitch(device->currentKeymapScript(), event->button())) {')
s = s.replace('if (event->button() == Qt::RightButton && device && !device->isCurrentCustomKeymap()) {', 'if (event->button() == Qt::RightButton && device && !device->isCurrentCustomKeymap()\n        && !InputBinding::isMouseSwitch(device->currentKeymapScript(), event->button())) {')
p.write_text(s)
p = root / 'tests/CMakeLists.txt'
s = p.read_text() + '''
# Real Qt mouse events and fake Controller transport; no phone required.
add_executable(mousebinding_ui_tests mousebinding_ui_test.cpp
    ../QtScrcpy/ui/keymapeditor.cpp ../QtScrcpy/ui/keymapeditor.h)
target_include_directories(mousebinding_ui_tests PRIVATE ../QtScrcpy/ui)
target_link_libraries(mousebinding_ui_tests PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
foreach(case_name IN ITEMS menu direct left_focus keyboard alias_conflict invalid namespaces editor_roundtrip toggle_routing)
    add_test(NAME mouseui_${case_name} COMMAND mousebinding_ui_tests ${case_name})
    set_tests_properties(mouseui_${case_name} PROPERTIES TIMEOUT 15)
endforeach()
add_executable(mousemapping_tests ${CORE}/tests/mousemapping_test.cpp)
target_include_directories(mousemapping_tests PRIVATE ${CORE}/src/common ${CORE}/src/device/android
    ${CORE}/src/device/controller ${CORE}/src/device/controller/inputconvert)
target_link_libraries(mousemapping_tests PRIVATE QtScrcpyCore Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
foreach(case_name IN ITEMS left right middle back forward aliases drag multi twice android joystick namespaces move_no_leak toggle simultaneous macro pause toggle_cancels_delayed uhid_priority)
    add_test(NAME mouse_${case_name} COMMAND mousemapping_tests ${case_name})
    set_tests_properties(mouse_${case_name} PROPERTIES TIMEOUT 15)
endforeach()
if(MSVC)
    target_compile_options(mousebinding_ui_tests PRIVATE /utf-8 /W3 /WX)
    target_compile_options(mousemapping_tests PRIVATE /utf-8 /W3 /WX)
endif()
'''
p.write_text(s)
