package io.github.mk0000001.lumaplayer;

import android.app.Activity;
import android.content.Context;
import android.graphics.Typeface;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioProfile;
import android.media.MediaCodecInfo;
import android.media.MediaCodecInfo.CodecProfileLevel;
import android.media.MediaCodecList;
import android.os.Build;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.StyleSpan;
import android.view.Display;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.analytics.AnalyticsListener;
import androidx.media3.exoplayer.audio.AudioSink;
import java.util.LinkedHashSet;
import java.util.Set;

@UnstableApi
final class PlaybackInfo implements AnalyticsListener {
    private String videoDecoder;
    private AudioSink.AudioTrackConfig audioTrack;
    private boolean firstVideoFrame;

    @Override
    public void onVideoDecoderInitialized(EventTime time, String decoderName,
                                          long initializedAtMs, long durationMs) {
        videoDecoder = decoderName;
    }

    @Override
    public void onVideoDecoderReleased(EventTime time, String decoderName) {
        if (decoderName.equals(videoDecoder)) {
            videoDecoder = null;
        }
    }

    @Override
    public void onAudioTrackInitialized(EventTime time, AudioSink.AudioTrackConfig config) {
        audioTrack = config;
    }

    @Override
    public void onAudioTrackReleased(EventTime time, AudioSink.AudioTrackConfig config) {
        if (audioTrack == config) {
            audioTrack = null;
        }
    }

    @Override
    public void onRenderedFirstFrame(EventTime time, Object output, long renderTimeMs) {
        firstVideoFrame = true;
    }

    static CharSequence report(Activity activity, ExoPlayer player, PlaybackInfo playback) {
        SpannableStringBuilder report = new SpannableStringBuilder();
        if (player != null && playback != null) {
            String pending = activity.getString(R.string.not_yet_observed);
            append(report, activity, R.string.video_source_label, videoName(activity, player.getVideoFormat()));
            append(report, activity, R.string.audio_source_label, audioName(activity, player.getAudioFormat()));
            append(report, activity, R.string.video_decoder_label, playback.decoderName(activity));
            append(report, activity, R.string.audio_track_label, playback.audioTrack == null ? pending
                    : activity.getString(R.string.audio_track_format, encodingName(playback.audioTrack.encoding),
                            playback.audioTrack.sampleRate));
            append(report, activity, R.string.video_frame_label, playback.firstVideoFrame
                    ? activity.getString(R.string.video_frame_rendered) : pending);
        }
        append(report, activity, R.string.display_hdr_label, displayHdr(activity));
        append(report, activity, R.string.hardware_hdr_label, hardwareHdr(activity));
        int audioLabel = Build.VERSION.SDK_INT >= 33 ? R.string.direct_audio_label : R.string.connected_audio_label;
        append(report, activity, audioLabel, directAudio(activity));
        report.append("\n\n").append(activity.getText(R.string.capability_footnote));
        return report;
    }

    private static void append(SpannableStringBuilder report, Context context, int label, String value) {
        if (report.length() > 0) {
            report.append("\n\n");
        }
        int start = report.length();
        report.append(context.getText(label));
        report.setSpan(new StyleSpan(Typeface.BOLD), start, report.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        report.append('\n').append(value);
    }

    @SuppressWarnings("deprecation")
    private static String displayHdr(Activity activity) {
        Display display = Build.VERSION.SDK_INT >= 30 ? activity.getDisplay()
                : activity.getWindowManager().getDefaultDisplay();
        if (display == null) {
            return activity.getString(R.string.unavailable);
        }
        int[] types = Build.VERSION.SDK_INT >= 34 ? display.getMode().getSupportedHdrTypes()
                : display.getHdrCapabilities().getSupportedHdrTypes();
        Set<String> formats = new LinkedHashSet<>();
        for (int type : types) {
            switch (type) {
                case Display.HdrCapabilities.HDR_TYPE_DOLBY_VISION: formats.add("Dolby Vision"); break;
                case Display.HdrCapabilities.HDR_TYPE_HDR10: formats.add("HDR10"); break;
                case Display.HdrCapabilities.HDR_TYPE_HDR10_PLUS: formats.add("HDR10+"); break;
                case Display.HdrCapabilities.HDR_TYPE_HLG: formats.add("HLG"); break;
                default: break;
            }
        }
        return formatList(activity, formats);
    }

    private static String hardwareHdr(Context context) {
        if (Build.VERSION.SDK_INT < 29) {
            return context.getString(R.string.unavailable);
        }
        Set<String> formats = new LinkedHashSet<>();
        for (MediaCodecInfo decoder : new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos()) {
            if (decoder.isEncoder() || !decoder.isHardwareAccelerated()) {
                continue;
            }
            for (String type : decoder.getSupportedTypes()) {
                if (!MimeTypes.VIDEO_DOLBY_VISION.equalsIgnoreCase(type) && !MimeTypes.VIDEO_H265.equalsIgnoreCase(type)
                        && !MimeTypes.VIDEO_AV1.equalsIgnoreCase(type) && !MimeTypes.VIDEO_VP9.equalsIgnoreCase(type)) {
                    continue;
                }
                for (CodecProfileLevel level : decoder.getCapabilitiesForType(type).profileLevels) {
                    String format = hdrProfile(type, level.profile);
                    if (format != null) {
                        formats.add(format);
                    }
                }
            }
        }
        return formatList(context, formats);
    }

    private static String hdrProfile(String mimeType, int profile) {
        if (MimeTypes.VIDEO_DOLBY_VISION.equalsIgnoreCase(mimeType)) {
            return "Dolby Vision";
        }
        if ((MimeTypes.VIDEO_H265.equalsIgnoreCase(mimeType) && profile == CodecProfileLevel.HEVCProfileMain10HDR10Plus)
                || (MimeTypes.VIDEO_AV1.equalsIgnoreCase(mimeType) && profile == CodecProfileLevel.AV1ProfileMain10HDR10Plus)
                || (MimeTypes.VIDEO_VP9.equalsIgnoreCase(mimeType) && (profile == CodecProfileLevel.VP9Profile2HDR10Plus
                        || profile == CodecProfileLevel.VP9Profile3HDR10Plus))) {
            return "HDR10+";
        }
        if ((MimeTypes.VIDEO_H265.equalsIgnoreCase(mimeType) && profile == CodecProfileLevel.HEVCProfileMain10HDR10)
                || (MimeTypes.VIDEO_AV1.equalsIgnoreCase(mimeType) && profile == CodecProfileLevel.AV1ProfileMain10HDR10)) {
            return "HDR10";
        }
        if (MimeTypes.VIDEO_VP9.equalsIgnoreCase(mimeType) && (profile == CodecProfileLevel.VP9Profile2HDR
                || profile == CodecProfileLevel.VP9Profile3HDR)) {
            return "VP9 HDR";
        }
        return null;
    }

    private static String directAudio(Context context) {
        AudioManager audioManager = context.getSystemService(AudioManager.class);
        if (audioManager == null) {
            return context.getString(R.string.unavailable);
        }
        Set<String> formats = new LinkedHashSet<>();
        if (Build.VERSION.SDK_INT >= 33) {
            android.media.AudioAttributes attributes = new android.media.AudioAttributes.Builder()
                    .setUsage(android.media.AudioAttributes.USAGE_MEDIA)
                    .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MOVIE)
                    .build();
            for (AudioProfile profile : audioManager.getDirectProfilesForAttributes(attributes)) {
                formats.add(encodingName(profile.getFormat()));
            }
        } else {
            for (AudioDeviceInfo device : audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                for (int encoding : device.getEncodings()) {
                    formats.add(encodingName(encoding));
                }
            }
        }
        return formatList(context, formats);
    }

    private static String formatList(Context context, Set<String> formats) {
        return formats.isEmpty() ? context.getString(R.string.none_reported) : String.join(", ", formats);
    }

    private static String videoName(Context context, Format format) {
        if (format == null || format.sampleMimeType == null) {
            return context.getString(R.string.not_yet_observed);
        }
        String name = format.sampleMimeType;
        if (MimeTypes.VIDEO_DOLBY_VISION.equals(name)) {
            name = "Dolby Vision";
        } else {
            if (MimeTypes.VIDEO_AV1.equals(name)) {
                name = "AV1";
            }
            if (format.colorInfo != null) {
                if (format.colorInfo.colorTransfer == C.COLOR_TRANSFER_ST2084) {
                    name = context.getString(R.string.format_with_detail, name, "PQ");
                } else if (format.colorInfo.colorTransfer == C.COLOR_TRANSFER_HLG) {
                    name = context.getString(R.string.format_with_detail, name, "HLG");
                } else if (format.colorInfo.colorTransfer == C.COLOR_TRANSFER_SDR) {
                    name = context.getString(R.string.format_with_detail, name, "SDR");
                }
            }
        }
        if (format.width > 0 && format.height > 0) {
            name = context.getString(R.string.format_with_detail, name, format.width + "×" + format.height);
        }
        return name;
    }

    private static String audioName(Context context, Format format) {
        if (format == null || format.sampleMimeType == null) {
            return context.getString(R.string.not_yet_observed);
        }
        return switch (format.sampleMimeType) {
            case MimeTypes.AUDIO_AC3 -> "Dolby Digital";
            case MimeTypes.AUDIO_E_AC3 -> "Dolby Digital Plus";
            case MimeTypes.AUDIO_E_AC3_JOC -> "Dolby Digital Plus JOC (Atmos)";
            case MimeTypes.AUDIO_TRUEHD -> "Dolby TrueHD";
            case MimeTypes.AUDIO_DTS -> "DTS";
            case MimeTypes.AUDIO_DTS_HD -> "DTS-HD";
            case MimeTypes.AUDIO_DTS_EXPRESS -> "DTS Express";
            case MimeTypes.AUDIO_DTS_UHD_P2 -> "DTS-UHD P2";
            default -> format.sampleMimeType;
        };
    }

    private static String encodingName(int encoding) {
        return switch (encoding) {
            case AudioFormat.ENCODING_PCM_8BIT, AudioFormat.ENCODING_PCM_16BIT,
                    AudioFormat.ENCODING_PCM_FLOAT, AudioFormat.ENCODING_PCM_24BIT_PACKED,
                    AudioFormat.ENCODING_PCM_32BIT -> "PCM";
            case AudioFormat.ENCODING_AC3 -> "Dolby Digital";
            case AudioFormat.ENCODING_E_AC3 -> "Dolby Digital Plus";
            case AudioFormat.ENCODING_E_AC3_JOC -> "Dolby Digital Plus JOC (Atmos)";
            case AudioFormat.ENCODING_DOLBY_TRUEHD -> "Dolby TrueHD";
            case AudioFormat.ENCODING_DTS -> "DTS";
            case AudioFormat.ENCODING_DTS_HD -> "DTS-HD";
            case AudioFormat.ENCODING_DTS_HD_MA -> "DTS-HD MA";
            case AudioFormat.ENCODING_DTS_UHD_P1 -> "DTS-UHD P1";
            case AudioFormat.ENCODING_DTS_UHD_P2 -> "DTS-UHD P2";
            case AudioFormat.ENCODING_AAC_LC, AudioFormat.ENCODING_AAC_HE_V1, AudioFormat.ENCODING_AAC_HE_V2,
                    AudioFormat.ENCODING_AAC_ELD, AudioFormat.ENCODING_AAC_XHE -> "AAC";
            default -> Integer.toString(encoding);
        };
    }

    private String decoderName(Context context) {
        if (videoDecoder == null) {
            return context.getString(R.string.not_yet_observed);
        }
        if (Build.VERSION.SDK_INT >= 29) {
            for (MediaCodecInfo decoder : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()) {
                if (videoDecoder.equals(decoder.getName())) {
                    if (decoder.isHardwareAccelerated()) {
                        return context.getString(R.string.decoder_hardware, videoDecoder);
                    }
                    if (decoder.isSoftwareOnly()) {
                        return context.getString(R.string.decoder_software, videoDecoder);
                    }
                    break;
                }
            }
        }
        return videoDecoder;
    }
}
