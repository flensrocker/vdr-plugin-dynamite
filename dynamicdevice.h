#ifndef __DYNAMITEDEVICE_H
#define __DYNAMITEDEVICE_H

#include <vdr/dvbdevice.h>
#include <vdr/plugin.h>

enum eDynamicDeviceReturnCode { ddrcSuccess,
                                ddrcNoFreeDynDev,
                                ddrcAlreadyAttached,
                                ddrcNotFound,
                                ddrcIsPrimaryDevice,
                                ddrcIsReceiving,
                                ddrcNotAllowed,
                                ddrcNotSupported
                             };

class cDynamicDevice : public cDevice {
 friend class cPluginDynamite;
private:
  static cPlugin *dynamite;
  static int defaultGetTSTimeout;
  static int idleTimeoutMinutes;
  static int idleWakeupHours;
  static cString *idleHook;

  static int numDynamicDevices;
  static cMutex arrayMutex;
  static cDynamicDevice *dynamicdevice[MAXDEVICES];
public:
  static cDvbDeviceProbe *dvbprobe;
  static bool enableOsdMessages;
  static int IndexOf(const char *DevPath, int &NextFreeIndex, int WishIndex);
  static int NumDynamicDevices(void) { return numDynamicDevices; }
         ///< Returns the total number of dynamic devices.
  static cDynamicDevice *GetDynamicDevice(int Index);
  static bool ProcessQueuedCommands(void);
  static int GetProposedCardIndex(const char *DevPath);
  static void DetachAllDevices(bool Force);
  static cString ListAllDevices(int &ReplyCode); // for SVDRP command LSTD
  static cString AttachDevicePattern(const char *Pattern);
  static eDynamicDeviceReturnCode AttachDevice(const char *DevPath);
  static eDynamicDeviceReturnCode DetachDevice(const char *DevPath, bool Force);
  static eDynamicDeviceReturnCode SetLockDevice(const char *DevPath, bool Lock);
  static eDynamicDeviceReturnCode SetIdle(const char *DevPath, bool Idle);
  static void AutoIdle(void);
  static eDynamicDeviceReturnCode SetGetTSTimeout(const char *DevPath, int Seconds);
  static void SetDefaultGetTSTimeout(int Seconds);
  static eDynamicDeviceReturnCode SetGetTSTimeoutHandlerArg(const char *DevPath, const char *Arg);
  static bool IsAttached(const char *DevPath);
private:
  int index;
  cString *devpath;
  cString *getTSTimeoutHandlerArg;
  bool     isDetachable;
  time_t   getTSWatchdog;
  int      getTSTimeout;
  bool     restartSectionHandler;
  time_t   lastCloseDvr; // for auto-idle
  time_t   idleSince;
  void ReadUdevProperties(void);
  void InternSetGetTSTimeout(int Seconds);
  void InternSetGetTSTimeoutHandlerArg(const char *Arg);
  void InternSetLock(bool Lock);
public:
  cDynamicDevice();
  const char *GetDevPath(void) const;
  void DeleteSubDevice(void);
  bool IsDetachable(void) const { return isDetachable; }
  virtual bool SetIdleDevice(bool Idle, bool TestOnly);
  virtual bool CanScanForEPG(void) const;
protected:
  virtual ~cDynamicDevice();
  virtual bool Ready(void);
  virtual void MakePrimaryDevice(bool On);
public:
  virtual bool HasDecoder(void) const;
  virtual bool AvoidRecording(void) const;
  virtual cSpuDecoder *GetSpuDecoder(void);
  virtual bool ProvidesSource(int Source) const;
  virtual bool ProvidesTransponder(const cChannel *Channel) const;
  virtual bool ProvidesTransponderExclusively(const cChannel *Channel) const;
  virtual bool ProvidesChannel(const cChannel *Channel, int Priority = -1, bool *NeedsDetachReceivers = NULL) const;
  virtual int NumProvidedSystems(void) const;
  virtual int SignalStrength(void) const;
  virtual int SignalQuality(void) const;
  virtual const cChannel *GetCurrentlyTunedTransponder(void) const;
  virtual bool IsTunedToTransponder(const cChannel *Channel);
  virtual bool MaySwitchTransponder(void);
protected:
  virtual bool SetChannelDevice(const cChannel *Channel, bool LiveView);
public:
  virtual bool HasLock(int TimeoutMs = 0);
  virtual bool HasProgramme(void);
protected:
  virtual bool SetPid(cPidHandle *Handle, int Type, bool On);
public:
  virtual int OpenFilter(u_short Pid, u_char Tid, u_char Mask);
  virtual void CloseFilter(int Handle);
  virtual bool HasCi(void);
  virtual uchar *GrabImage(int &Size, bool Jpeg = true, int Quality = -1, int SizeX = -1, int SizeY = -1);
  virtual void SetVideoDisplayFormat(eVideoDisplayFormat VideoDisplayFormat);
  virtual void SetVideoFormat(bool VideoFormat16_9);
  virtual eVideoSystem GetVideoSystem(void);
  virtual void GetVideoSize(int &Width, int &Height, double &VideoAspect);
  virtual void GetOsdSize(int &Width, int &Height, double &PixelAspect);
protected:
  virtual void SetAudioTrackDevice(eTrackType Type);
  virtual void SetSubtitleTrackDevice(eTrackType Type);
  virtual int GetAudioChannelDevice(void);
  virtual void SetAudioChannelDevice(int AudioChannel);
  virtual void SetVolumeDevice(int Volume);
  virtual void SetDigitalAudioDevice(bool On);
  virtual bool CanReplay(void) const;
  virtual bool SetPlayMode(ePlayMode PlayMode);
  virtual int PlayVideo(const uchar *Data, int Length);
  virtual int PlayAudio(const uchar *Data, int Length, uchar Id);
  virtual int PlaySubtitle(const uchar *Data, int Length);
  virtual int PlayPesPacket(const uchar *Data, int Length, bool VideoOnly = false);
  virtual int PlayTsVideo(const uchar *Data, int Length);
  virtual int PlayTsAudio(const uchar *Data, int Length);
  virtual int PlayTsSubtitle(const uchar *Data, int Length);
public:
  virtual int64_t GetSTC(void);
  virtual bool IsPlayingVideo(void) const;
  virtual bool HasIBPTrickSpeed(void);
  virtual void TrickSpeed(int Speed);
  virtual void Clear(void);
  virtual void Play(void);
  virtual void Freeze(void);
  virtual void Mute(void);
  virtual void StillPicture(const uchar *Data, int Length);
  virtual bool Poll(cPoller &Poller, int TimeoutMs = 0);
  virtual bool Flush(int TimeoutMs = 0);
  virtual int PlayPes(const uchar *Data, int Length, bool VideoOnly = false);
  virtual int PlayTs(const uchar *Data, int Length, bool VideoOnly = false);
protected:
  virtual bool OpenDvr(void);
  virtual void CloseDvr(void);
  virtual bool GetTSPacket(uchar *&Data);
  
#ifdef YAVDR_PATCHES
//opt-21_internal-cam-devices.dpatch
  virtual bool HasInternalCam(void);
//opt-44_rotor.dpatch 
  virtual bool SendDiseqcCmd(dvb_diseqc_master_cmd cmd);
//opt-64_lnb-sharing.dpatch 
  virtual void SetLnbNrFromSetup(void);
  virtual int LnbNr(void) const;
  virtual bool IsShareLnb(const cDevice *Device);
  virtual bool IsLnbConflict(const cChannel *Channel);
#endif
  };

#endif //__DYNAMITEDEVICE_H
