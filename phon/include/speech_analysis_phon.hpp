#ifndef SPEECH_ANALYSIS_PHON_SCRIPT_INCLUDE
#define SPEECH_ANALYSIS_PHON_SCRIPT_INCLUDE

static const char *speech_analysis_script = R"_(
function report_intensity(time as Number)
let sound = get_current_sound()

if sound then
print "Intensity at time ", time, " s:"
let channels = get_visible_channels()

foreach channel in channels do
let dB = get_intensity(sound, channel, time)

if channel == 0 then
print "Average over all channels: ", dB, " dB"
else
print "Channel ", channel, ": ", dB, " dB"
end
end
else
alert("No sound or annotation view is currently selected!")
end
end

function report_mean_intensity(t1 as Number, t2 as Number)
let sound = get_current_sound()

if sound then
print "Mean intensity from ", t1, " to ", t2, ":"
let channels = get_visible_channels()

foreach channel in channels do
let f0 = get_mean_intensity(sound, channel, t1, t2)

if f0 then
f0 = f0 & "dB"
else
f0 = "undefined"
end

if channel == 0 then
print "Average over all channels: ", f0
else
print "Channel ", channel, ": ", f0
end
end
else
alert("No sound or annotation view is currently selected!")
end
end

function report_pitch(time as Number)
let sound = get_current_sound()

if sound then
print "Pitch at time ", time, " s:"
let channels = get_visible_channels()

foreach channel in channels do
let f0 = get_pitch(sound, channel, time)

if f0 then
f0 = f0 & "Hz"
else
f0 = "undefined"
end

if channel == 0 then
print "Average over all channels: ", f0
else
print "Channel ", channel, ": ", f0
end
end
else
alert("No sound or annotation view is currently selected!")
end
end

function report_mean_pitch(t1 as Number, t2 as Number)
let sound = get_current_sound()

if sound then
print "Mean pitch from ", t1, " to ", t2, ":"
let channels = get_visible_channels()

foreach channel in channels do
let f0 = get_mean_pitch(sound, channel, t1, t2)

if f0 then
f0 = f0 & "Hz"
else
f0 = "undefined"
end

if channel == 0 then
print "Average over all channels: ", f0
else
print "Channel ", channel, ": ", f0
end
end
else
alert("No sound or annotation view is currently selected!")
end
end

function report_formants(time as Number)
let sound = get_current_sound()

if sound then
print "Formants at time ", time, " s:"
let channels = get_visible_channels()

foreach channel in channels do
if channel == 0 then
print "Average over all channels:"
else
print "Channel ", channel, ":"
end
let result = get_formants(sound, channel, time)
let nformant = result.nrow

for i = 1 to nformant do
let label = "F" & i
let fmt = result[i, 1]
let bw = result[i, 2]

if fmt then
print label, " = ", fmt, " Hz\t bandwidth = ", bw, " Hz"
else
print label, " = undefined"
end
end
end
else
alert("No sound or annotation view is currently selected!")
end
end

function report_mean_formants(t1 as Number, t2 as Number)
alert("Not implemented yet!")
end

)_";

#endif /* SPEECH_ANALYSIS_PHON_SCRIPT_INCLUDE */