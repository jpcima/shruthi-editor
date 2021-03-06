// Shruthi-Editor: An unofficial Editor for the Shruthi hardware synthesizer. For
// informations about the Shruthi, see <http://www.mutable-instruments.net/shruthi1>.
//
// Copyright (C) 2011-2018 Manuel Krönig
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.


#ifndef SHRUTHI_LIBRARY_H
#define SHRUTHI_LIBRARY_H


#include <QObject>
#include "patch.h"
#include "sequence.h"
class MidiOut;
class QString;
class QTime;


class Library : public QObject {
        Q_OBJECT

    public:
        Library(MidiOut *out);
        ~Library();

        void setFirmwareVersion(const int &version);
        void setFirmwareVersionRequested();
        void setMidiChannel(const unsigned char &channel);

        const Patch &recallPatch(const int &id) const;
        void storePatch(const int &id, const Patch &patch);
        void listPatches() const;
        void movePatch(const int &from, const int &to);

        bool patchMoved(const int &id) const;
        bool patchEdited(const int &id) const;
        QString getPatchIdentifier(const int &id) const;

        const Sequence &recallSequence(const int &id) const;
        void storeSequence(const int &id, const Sequence &sequence);
        void listSequences() const;
        void moveSequence(const int &from, const int &to);

        bool sequenceMoved(const int &id) const;
        bool sequenceEdited(const int &id) const;
        bool sequenceIsInit(const int &id) const;
        QString getSequenceIdentifier(const int &id) const;

        bool startFetching(const int &flags, const int &from, const int &to);
        void abortFetching();
        QString fetchProgress() const;

        bool receivedPatch(const unsigned char *sysex);
        bool isFetchingPatches() const;

        bool receivedSequence(const unsigned char *seq);
        bool isFetchingSequences() const;

        bool startSending(const int &flags, const int &from, const int &to);
        void abortSending();
        QString sendProgress() const;
        bool keepSending();
        bool isSending();
        const int &sendTimeout();
        const int &sendRedrawIndex();
        const int &sendRedrawFlags();

        void remove(const int &from, const int &to);
        void insert(const int &id);
        void reset(const int &flags, const int &from, const int &to);

        bool saveLibrary(const QString &path);
        bool loadLibrary(const QString &path, bool append = false);

        const int &getNumberOfPrograms() const;
        const int &getNumberOfHWPrograms() const;
        void setNumberOfHWPrograms(const int &num);

        const unsigned int &nextPatch() const;
        const unsigned int &nextSequence() const;

        void rememberShruthiProgram(const int &patch, const int &sequence);
        bool recallShruthiProgramm();

    private:
        Library(const Library&); //forbid copying
        Library &operator=(const Library&); //forbid assignment

        bool keepFetching();

        std::vector<Patch> patches;
        std::vector<bool> mPatchMoved;
        std::vector<bool> mPatchEdited;

        std::vector<Sequence> sequences;
        std::vector<bool> mSequenceMoved;
        std::vector<bool> mSequenceEdited;

        const Sequence init_sequence;

        void growVectorsTo(const int &num);

        QString calculateSequenceHash(const unsigned int &id) const;
        static QString calculateHash(const unsigned char *key, const unsigned int &len);

        MidiOut *midiout;

        bool fetchPatchMode;
        bool fetchSequenceMode;

        unsigned int fetchStart;
        unsigned int fetchEnd;
        unsigned int fetchNextRequest;
        unsigned int fetchNextIncomingPatch;
        unsigned int fetchNextIncomingSequence;

        bool mSendPatchMode;
        bool mSendSequenceMode;
        unsigned int mSendStart;
        unsigned int mSendEnd;
        unsigned int mSendIndex;
        bool mForceSending;
        int mSendTimeout;
        bool mSendAlternate;
        int mSendRedrawIndex;
        int mSendRedrawFlags;

        QTime *time;

        int numberOfPrograms;
        int numberOfHWPrograms;

        int firmwareVersion;
        bool firmwareVersionRequested;

        unsigned char mMidiChannel;

        int mCurrentShruthiPatch;
        int mCurrentShruthiSequence;
        bool mRememberedCurrentShruthiProgram;

    signals:
        void displayStatusbar(QString);
};


#endif // SHRUTHI_LIBRARY_H
