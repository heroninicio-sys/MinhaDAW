#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <utility>
#include <vector>
#include "AudioTrack.h"

struct TimelineRegion
{
    juce::String name;
    double startSec = 0.0;
    double endSec = 8.0;
    juce::Colour colour;
};

class ProjectSerializer
{
public:
    static bool saveProject(const juce::File& file,
                            double bpm,
                            const std::vector<TimelineRegion>& regions,
                            juce::OwnedArray<AudioTrack>& tracks,
                            juce::CriticalSection& trackLock,
                            AudioTrack* masterTrack = nullptr,
                            float masterGain = 1.0f)
    {
        if (file == juce::File() || !file.getParentDirectory().createDirectory())
            return false;

        const juce::ScopedLock sl(trackLock);

        auto* rootObj = new juce::DynamicObject();
        rootObj->setProperty("version", "1.4");
        rootObj->setProperty("bpm", bpm);

        juce::Array<juce::var> regionsArr;
        for (const auto& reg : regions)
        {
            auto* rObj = new juce::DynamicObject();
            rObj->setProperty("name", reg.name);
            rObj->setProperty("startSec", reg.startSec);
            rObj->setProperty("endSec", reg.endSec);
            rObj->setProperty("colour", reg.colour.toString());
            regionsArr.add(juce::var(rObj));
        }
        rootObj->setProperty("regions", regionsArr);

        juce::Array<juce::var> tracksArray;

        for (auto* track : tracks)
        {
            auto* tObj = new juce::DynamicObject();
            tObj->setProperty("name", track->getName());
            tObj->setProperty("gain", track->gain.load());
            tObj->setProperty("pan", track->pan.load());
            tObj->setProperty("muted", track->isMuted.load());
            tObj->setProperty("soloed", track->isSoloed.load());
            tObj->setProperty("armed", track->isArmed.load());
            tObj->setProperty("monitoring", track->isMonitoring.load());

            juce::Array<juce::var> audioClipsArr;
            for (auto* c : track->getClips())
            {
                auto* cObj = new juce::DynamicObject();
                cObj->setProperty("filePath", c->audioFile.getFullPathName());
                cObj->setProperty("timelineStart", c->timelineStart);
                cObj->setProperty("sourceStartOffset", c->sourceStartOffset);
                cObj->setProperty("duration", c->duration);
                cObj->setProperty("fadeIn", c->fadeInDuration);
                cObj->setProperty("fadeOut", c->fadeOutDuration);
                cObj->setProperty("clipGain", c->clipGain);
                audioClipsArr.add(juce::var(cObj));
            }
            tObj->setProperty("audioClips", audioClipsArr);

            juce::Array<juce::var> midiClipsArr;
            for (auto* mc : track->getMidiClips())
            {
                auto* mcObj = new juce::DynamicObject();
                mcObj->setProperty("startBeats", mc->timelineStartBeats);
                mcObj->setProperty("durationBeats", mc->durationBeats);

                juce::Array<juce::var> notesArr;
                for (const auto& n : mc->notes)
                {
                    auto* nObj = new juce::DynamicObject();
                    nObj->setProperty("note", n.noteNumber);
                    nObj->setProperty("start", n.startBeat);
                    nObj->setProperty("len", n.lengthBeats);
                    nObj->setProperty("vel", n.velocity);
                    notesArr.add(juce::var(nObj));
                }
                mcObj->setProperty("notes", notesArr);
                midiClipsArr.add(juce::var(mcObj));
            }
            tObj->setProperty("midiClips", midiClipsArr);

            juce::Array<juce::var> nativeFxArr;
            for (auto* fx : track->getNativeEffects())
            {
                auto* fxObj = new juce::DynamicObject();
                fxObj->setProperty("typeId", fx->getTypeId());
                fxObj->setProperty("bypassed", fx->bypassed.load());
                fxObj->setProperty("state", fx->saveState());
                nativeFxArr.add(juce::var(fxObj));
            }
            tObj->setProperty("nativeEffects", nativeFxArr);

            juce::Array<juce::var> vstArr;
            for (int i = 0; i < track->getVstPlugins().size(); ++i)
            {
                auto* vObj = new juce::DynamicObject();
                vObj->setProperty("path", i < track->vstFilePaths.size() ? track->vstFilePaths[i] : "");
                vObj->setProperty("bypassed", track->isVstPluginBypassed(i));

                juce::MemoryBlock stateMem;
                track->getVstPlugins()[i]->getStateInformation(stateMem);
                vObj->setProperty("stateBase64", stateMem.toBase64Encoding());
                vstArr.add(juce::var(vObj));
            }
            tObj->setProperty("vstPlugins", vstArr);

            tracksArray.add(juce::var(tObj));
        }

        rootObj->setProperty("tracks", tracksArray);

        auto* masterObj = new juce::DynamicObject();
        masterObj->setProperty("gain", juce::jlimit(0.0f, 1.5f, masterGain));
        juce::Array<juce::var> masterFxArray;
        juce::Array<juce::var> masterVstArray;
        if (masterTrack != nullptr)
        {
            for (auto* fx : masterTrack->getNativeEffects())
            {
                auto* fxObj = new juce::DynamicObject();
                fxObj->setProperty("typeId", fx->getTypeId());
                fxObj->setProperty("bypassed", fx->bypassed.load());
                fxObj->setProperty("state", fx->saveState());
                masterFxArray.add(juce::var(fxObj));
            }
            for (int i = 0; i < masterTrack->getVstPlugins().size(); ++i)
            {
                auto* vstObj = new juce::DynamicObject();
                vstObj->setProperty("path", i < masterTrack->vstFilePaths.size() ? masterTrack->vstFilePaths[i] : "");
                vstObj->setProperty("bypassed", masterTrack->isVstPluginBypassed(i));
                juce::MemoryBlock stateMem;
                masterTrack->getVstPlugins()[i]->getStateInformation(stateMem);
                vstObj->setProperty("stateBase64", stateMem.toBase64Encoding());
                masterVstArray.add(juce::var(vstObj));
            }
        }
        masterObj->setProperty("nativeEffects", masterFxArray);
        masterObj->setProperty("vstPlugins", masterVstArray);
        rootObj->setProperty("masterTrack", juce::var(masterObj));

        juce::String jsonText = juce::JSON::toString(juce::var(rootObj), true);
        return file.replaceWithText(jsonText);
    }

    static bool loadProject(const juce::File& file,
                            double& outBpm,
                            std::vector<TimelineRegion>& outRegions,
                            juce::OwnedArray<AudioTrack>& tracks,
                            juce::CriticalSection& trackLock,
                            juce::AudioFormatManager& audioFmtMgr,
                            juce::AudioPluginFormatManager& pluginFmtMgr,
                            juce::TimeSliceThread& diskThread,
                            double sampleRate,
                            int blockSize,
                            AudioTrack* masterTrack = nullptr,
                            float* outMasterGain = nullptr)
    {
        if (!file.existsAsFile())
            return false;

        auto parsed = juce::JSON::parse(file);
        auto* rootObj = parsed.getDynamicObject();
        if (rootObj == nullptr)
            return false;

        auto* tracksArr = rootObj->getProperty("tracks").getArray();
        if (tracksArr == nullptr)
            return false;

        for (const auto& trackVar : *tracksArr)
            if (trackVar.getDynamicObject() == nullptr)
                return false;

        const juce::ScopedLock sl(trackLock);
        juce::OwnedArray<AudioTrack> loadedTracks;
        juce::OwnedArray<NativeEffect> loadedMasterEffects;
        juce::OwnedArray<juce::AudioPluginInstance> loadedMasterPlugins;
        juce::Array<bool> loadedMasterVstBypassed;
        juce::StringArray loadedMasterPluginPaths;
        std::vector<TimelineRegion> loadedRegions;
        double loadedBpm = (double)rootObj->getProperty("bpm");
        if (!std::isfinite(loadedBpm) || loadedBpm < 20.0 || loadedBpm > 300.0)
            loadedBpm = 120.0;

        if (auto* regArr = rootObj->getProperty("regions").getArray())
        {
            for (const auto& rVar : *regArr)
            {
                if (auto* rObj = rVar.getDynamicObject())
                {
                    loadedRegions.push_back({
                        rObj->getProperty("name").toString(),
                        (double)rObj->getProperty("startSec"),
                        (double)rObj->getProperty("endSec"),
                        juce::Colour::fromString(rObj->getProperty("colour").toString())
                    });
                }
            }
        }

        for (const auto& tVar : *tracksArr)
        {
            auto* tObj = tVar.getDynamicObject();
            if (tObj == nullptr) continue;

            auto* track = loadedTracks.add(new AudioTrack(tObj->getProperty("name").toString(), audioFmtMgr, diskThread));
            track->prepareToPlay(blockSize, sampleRate);
            track->gain.store((float)tObj->getProperty("gain"));
            track->pan.store(tObj->hasProperty("pan") ? (float)tObj->getProperty("pan") : 0.0f);
            track->isMuted.store((bool)tObj->getProperty("muted"));
            track->isSoloed.store((bool)tObj->getProperty("soloed"));
            track->isArmed.store((bool)tObj->getProperty("armed"));
            track->isMonitoring.store(tObj->hasProperty("monitoring") ? (bool)tObj->getProperty("monitoring") : false);

            if (auto* aClips = tObj->getProperty("audioClips").getArray())
            {
                for (const auto& cVar : *aClips)
                {
                    if (auto* cObj = cVar.getDynamicObject())
                    {
                        juce::File audioFile(cObj->getProperty("filePath").toString());
                        if (audioFile.existsAsFile())
                        {
                            auto clip = std::make_unique<AudioClip>(audioFile, audioFmtMgr, (double)cObj->getProperty("timelineStart"));
                            clip->sourceStartOffset = (double)cObj->getProperty("sourceStartOffset");
                            clip->duration          = (double)cObj->getProperty("duration");
                            clip->fadeInDuration    = (double)cObj->getProperty("fadeIn");
                            clip->fadeOutDuration   = (double)cObj->getProperty("fadeOut");
                            clip->clipGain          = cObj->hasProperty("clipGain") ? (float)cObj->getProperty("clipGain") : 1.0f;
                            track->getClips().add(std::move(clip));
                        }
                    }
                }
            }

            if (auto* mClips = tObj->getProperty("midiClips").getArray())
            {
                for (const auto& mcVar : *mClips)
                {
                    if (auto* mcObj = mcVar.getDynamicObject())
                    {
                        auto mc = std::make_unique<MidiClip>((double)mcObj->getProperty("startBeats"),
                                                             (double)mcObj->getProperty("durationBeats"));
                        if (auto* nArr = mcObj->getProperty("notes").getArray())
                        {
                            for (const auto& nVar : *nArr)
                            {
                                if (auto* nObj = nVar.getDynamicObject())
                                {
                                    mc->notes.emplace_back((int)nObj->getProperty("note"),
                                                           (double)nObj->getProperty("start"),
                                                           (double)nObj->getProperty("len"),
                                                           (float)nObj->getProperty("vel"));
                                }
                            }
                        }
                        track->getMidiClips().add(std::move(mc));
                    }
                }
            }

            if (auto* fxArr = tObj->getProperty("nativeEffects").getArray())
            {
                for (const auto& fxVar : *fxArr)
                {
                    if (auto* fxObj = fxVar.getDynamicObject())
                    {
                        juce::String typeId = fxObj->getProperty("typeId").toString();
                        std::unique_ptr<NativeEffect> fx;

                        if (typeId == "NativeEQ")            fx = std::make_unique<MultiChannelEQEffect>(16);
                        else if (typeId == "NativeComp")     fx = std::make_unique<CompressorEffect>();
                        else if (typeId == "NativeDist")     fx = std::make_unique<DistortionEffect>();
                        else if (typeId == "NativeAutoTune") fx = std::make_unique<AutoTuneEffect>();
                        else if (typeId == "NativeReverb")   fx = std::make_unique<ReverbEffect>();
                        else if (typeId == "NativeDelay")    fx = std::make_unique<DelayEffect>();

                        if (fx != nullptr)
                        {
                            fx->bypassed.store((bool)fxObj->getProperty("bypassed"));
                            fx->loadState(fxObj->getProperty("state"));
                            track->addNativeEffect(std::move(fx));
                        }
                    }
                }
            }

            if (auto* vstArr = tObj->getProperty("vstPlugins").getArray())
            {
                for (const auto& vVar : *vstArr)
                {
                    if (auto* vObj = vVar.getDynamicObject())
                    {
                        juce::File vstFile(vObj->getProperty("path").toString());
                        if (vstFile.exists())
                        {
                            juce::OwnedArray<juce::PluginDescription> descs;
                            for (int fmtIdx = 0; fmtIdx < pluginFmtMgr.getNumFormats(); ++fmtIdx)
                            {
                                pluginFmtMgr.getFormat(fmtIdx)->findAllTypesForFile(descs, vstFile.getFullPathName());
                                if (descs.size() > 0) break;
                            }

                            if (descs.size() > 0)
                            {
                                juce::String err;
                                if (auto inst = pluginFmtMgr.createPluginInstance(*descs[0], sampleRate, blockSize, err))
                                {
                                    juce::MemoryBlock mb;
                                    if (mb.fromBase64Encoding(vObj->getProperty("stateBase64").toString()))
                                        inst->setStateInformation(mb.getData(), (int)mb.getSize());

                                    track->vstFilePaths.add(vstFile.getFullPathName());
                                    track->addVstPlugin(std::move(inst));
                                }
                            }
                        }
                    }
                }
            }
        }

        float loadedMasterGain = 1.0f;
        if (auto* masterObj = rootObj->getProperty("masterTrack").getDynamicObject())
        {
            loadedMasterGain = (float)masterObj->getProperty("gain");
            if (!std::isfinite(loadedMasterGain)) loadedMasterGain = 1.0f;
            loadedMasterGain = juce::jlimit(0.0f, 1.5f, loadedMasterGain);

            if (auto* fxArr = masterObj->getProperty("nativeEffects").getArray())
            {
                for (const auto& fxVar : *fxArr)
                {
                    auto* fxObj = fxVar.getDynamicObject();
                    if (fxObj == nullptr) continue;
                    const auto typeId = fxObj->getProperty("typeId").toString();
                    std::unique_ptr<NativeEffect> fx;
                    if (typeId == "NativeEQ") fx = std::make_unique<MultiChannelEQEffect>(16);
                    else if (typeId == "NativeComp") fx = std::make_unique<CompressorEffect>();
                    else if (typeId == "NativeDist") fx = std::make_unique<DistortionEffect>();
                    else if (typeId == "NativeAutoTune") fx = std::make_unique<AutoTuneEffect>();
                    else if (typeId == "NativeReverb") fx = std::make_unique<ReverbEffect>();
                    else if (typeId == "NativeDelay") fx = std::make_unique<DelayEffect>();
                    if (fx != nullptr)
                    {
                        fx->bypassed.store((bool)fxObj->getProperty("bypassed"));
                        fx->loadState(fxObj->getProperty("state"));
                        loadedMasterEffects.add(std::move(fx));
                    }
                }
            }

            if (auto* vstArr = masterObj->getProperty("vstPlugins").getArray())
            {
                for (const auto& vstVar : *vstArr)
                {
                    auto* vstObj = vstVar.getDynamicObject();
                    if (vstObj == nullptr) continue;
                    juce::File vstFile(vstObj->getProperty("path").toString());
                    if (!vstFile.exists()) continue;
                    juce::OwnedArray<juce::PluginDescription> descriptions;
                    for (int i = 0; i < pluginFmtMgr.getNumFormats(); ++i)
                    {
                        pluginFmtMgr.getFormat(i)->findAllTypesForFile(descriptions, vstFile.getFullPathName());
                        if (!descriptions.isEmpty()) break;
                    }
                    if (descriptions.isEmpty()) continue;
                    juce::String error;
                    auto instance = pluginFmtMgr.createPluginInstance(*descriptions[0], sampleRate, blockSize, error);
                    if (instance == nullptr) continue;
                    juce::MemoryBlock stateMem;
                    if (stateMem.fromBase64Encoding(vstObj->getProperty("stateBase64").toString()))
                        instance->setStateInformation(stateMem.getData(), (int)stateMem.getSize());
                    const bool bypassed = vstObj->hasProperty("bypassed")
                        ? (bool)vstObj->getProperty("bypassed")
                        : (instance->getBypassParameter() != nullptr && instance->getBypassParameter()->getValue() > 0.5f);
                    loadedMasterPluginPaths.add(vstFile.getFullPathName());
                    loadedMasterPlugins.add(std::move(instance));
                    loadedMasterVstBypassed.add(bypassed);
                }
            }
        }

        tracks.clear(true);
        while (!loadedTracks.isEmpty())
            tracks.add(loadedTracks.removeAndReturn(0));
        outRegions = std::move(loadedRegions);
        outBpm = loadedBpm;

        if (masterTrack != nullptr)
        {
            masterTrack->getNativeEffects().clear(true);
            masterTrack->clearVstPlugins();
            while (!loadedMasterEffects.isEmpty())
                masterTrack->addNativeEffect(std::unique_ptr<NativeEffect>(loadedMasterEffects.removeAndReturn(0)));
            while (!loadedMasterPlugins.isEmpty())
                masterTrack->addVstPlugin(std::unique_ptr<juce::AudioPluginInstance>(loadedMasterPlugins.removeAndReturn(0)));
            masterTrack->vstFilePaths = loadedMasterPluginPaths;
            for (int i = 0; i < loadedMasterVstBypassed.size(); ++i)
                masterTrack->setVstPluginBypassed(i, loadedMasterVstBypassed[i]);
        }
        if (outMasterGain != nullptr)
            *outMasterGain = loadedMasterGain;
        return true;
    }
};
