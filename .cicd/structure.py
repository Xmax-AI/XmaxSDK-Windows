"""Check public header responsibilities and the stable SDK module boundary."""
from pathlib import Path
import re

PUBLIC_TYPES = {
    'RealtimeModel': 'RealtimeModel.hpp',
    'XmaxEnvironment': 'XmaxEnvironment.hpp',
    'RealtimeQualityLevel': 'RealtimeQualityLevel.hpp',
    'RealtimeCameraPreset': 'RealtimeCameraPreset.hpp',
    'XmaxConfiguration': 'XmaxConfiguration.hpp',
    'RealtimeConfiguration': 'RealtimeConfiguration.hpp',
    'error': 'error.hpp',
    'logger': 'logger.hpp',
    'string': 'string.hpp',
    'frame': 'frame.hpp',
    'camera_device': 'camera_device.hpp',
    'uploaded_file_info': 'uploaded_file_info.hpp',
    'uploaded_file': 'uploaded_file.hpp',
    'storage': 'storage.hpp',
    'realtime': 'realtime.hpp',
    'client': 'client.hpp',
}


def validate(root: Path) -> None:
    declarations = re.compile(r'^\s*(?:enum\s+class|class|struct)\s+(\w+)\b', re.MULTILINE)
    public = root / 'include/xmax'
    for name, filename in PUBLIC_TYPES.items():
        text = (public / filename).read_text(encoding='utf-8')
        assert declarations.findall(text) == [name], f'Unrelated public types in {filename}'
    umbrella = (public / 'xmax.hpp').read_text(encoding='utf-8')
    assert not declarations.findall(umbrella), 'xmax.hpp must only aggregate public headers'
    for filename in PUBLIC_TYPES.values():
        assert f'#include "{filename}"' in umbrella, f'Missing public header: {filename}'
    core = root / 'Sources/XmaxSDK/Core/Realtime/XmaxRealtimeManager.hpp'
    manager = core.read_text(encoding='utf-8')
    assert 'RealtimeTiming timing_;' in manager, 'Startup timing must have an independent owner'
    assert 'RealtimeErrorHandler errors_;' in manager, 'State error details must have an independent owner'
    stream = (root / 'Sources/XmaxSDK/Stream/StreamController.hpp').read_text(encoding='utf-8')
    room = (root / 'Sources/XmaxSDK/Stream/Room/RoomController.hpp').read_text(encoding='utf-8')
    assert 'std::shared_ptr<Rtc> rtc_;' not in stream, 'RTC room resources belong to RoomController'
    assert 'std::shared_ptr<Rtc> rtc_;' in room, 'RoomController must own its RTC backend'
    controller = (root / 'Sources/XmaxSDK/Media/Interaction/InteractionController.hpp').read_text(encoding='utf-8')
    assert 'std::log(' not in controller and 'std::clamp(' not in controller, 'Coordinate math belongs to its mapper'
