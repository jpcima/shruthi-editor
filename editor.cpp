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


#include "editor.h"
#ifdef DEBUGMSGS
#include <QDebug>
#endif
#include <QTimer>
#include <algorithm> // for max, min
#include <iostream>
#include <stddef.h> // for NULL
#include "fileio.h"
#include "flag.h"
#include "library.h"
#include "message.h"
#include "midi.h"
#include "midiout.h"
#include "patch.h"
#include "sequence.h"
#include "sequence_parameter.h"


Editor::Editor():
    midiout(new MidiOut),
    patch(new Patch),
    sequence(new Sequence),
    library(new Library(midiout)) {
#ifdef DEBUGMSGS
    qDebug("Editor::Editor()");
#endif
    shruthiFilterBoard = 0;
    firmwareVersion = 0;

    // Relay status bar messages:
    connect(library, SIGNAL(displayStatusbar(QString)), this, SIGNAL(displayStatusbar(QString)));
}


void Editor::run() {
#ifdef DEBUGMSGS
    qDebug("Editor::run()");
#endif
    emit setStatusbarVersionLabel(patch->getVersionString());
    redrawLibraryItems(Flag::PATCH|Flag::SEQUENCE, 0, library->getNumberOfPrograms() - 1);
    redrawAllPatchParameters();
    redrawAllSequenceParameters();
}


bool Editor::setMidiOutputPort(int out) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::setMidiPorts:" << out;
#endif
    bool status = midiout->open(out);
    emit midiOutputStatusChanged(status);
    return status;
}


void Editor::setMidiChannel(unsigned char channel) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::setMidiChannel:" << channel;
#endif
    Editor::channel = channel;
    library->setMidiChannel(channel);
}


void Editor::setShruthiFilterBoard(int filter) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::setShruthiFilterBoard:" << filter;
#endif
    Editor::shruthiFilterBoard = filter;
}


Editor::~Editor() {
#ifdef DEBUGMSGS
    qDebug() << "Editor::~Editor()";
#endif
    delete library;
    library = NULL;
    delete sequence;
    sequence = NULL;
    delete patch;
    patch = NULL;
    delete midiout;
    midiout = NULL;
}


void Editor::process(QueueItem item) {
    switch(item.action) {
        case QueueAction::PATCH_PARAMETER_CHANGE_EDITOR:
            actionPatchParameterChangeEditor(item.int0, item.int1);
            break;
        case QueueAction::SEQUENCE_PARAMETER_CHANGE_EDITOR:
            actionSequenceParameterChangeEditor(item.int0, item.int1);
            break;
        case QueueAction::SYSEX_FETCH_REQUEST:
            actionFetchRequest(item.int0);
            break;
        case QueueAction::SYSEX_SEND_DATA:
            actionSendData(item.int0);
            break;
        case QueueAction::SYSEX_SHRUTHI_INFO_REQUEST:
            actionShruthiInfoRequest();
            break;
        case QueueAction::PATCH_PARAMETER_CHANGE_MIDI:
            actionPatchParameterChangeMidi(item.int0, item.int1);
            break;
        case QueueAction::NOTE_ON:
            actionNoteOn(item.int0,item.int1);
            break;
        case QueueAction::NOTE_OFF:
            actionNoteOff(item.int0);
            break;
        case QueueAction::NOTE_PANIC:
            actionNotePanic();
            break;
        case QueueAction::SYSEX_RECEIVED:
            actionSysexReceived(item.int0, item.int1, item.size, item.message);
            break;
        case QueueAction::SET_PATCHNAME:
            actionSetPatchname(item.string);
            break;
        case QueueAction::FILEIO_LOAD:
            actionFileIOLoad(item.string, item.int0);
            break;
        case QueueAction::FILEIO_SAVE:
            actionFileIOSave(item.string, item.int0);
            break;
        case QueueAction::RESET_PATCH:
            actionResetPatch(item.int0);
            break;
        case QueueAction::LIBRARY_FETCH:
            actionLibraryFetch(item.int0, item.int1, item.int2);
            break;
        case QueueAction::LIBRARY_RECALL:
            actionLibraryRecall(item.int0, item.int1);
            break;
        case QueueAction::LIBRARY_STORE:
            actionLibraryStore(item.int0, item.int1);
            break;
        case QueueAction::LIBRARY_MOVE:
            actionLibraryMove(item.int0, item.int1, item.int2);
            break;
        case QueueAction::LIBRARY_SEND:
            actionLibrarySend(item.int0, item.int1, item.int2);
            break;
        case QueueAction::LIBRARY_REMOVE:
            actionLibraryRemove(item.int1, item.int2); // ignore flags (item.int0)
            break;
        case QueueAction::LIBRARY_INSERT:
            actionLibraryInsert(item.int0);
            break;
        case QueueAction::LIBRARY_RESET:
            actionLibraryReset(item.int0, item.int1, item.int2);
            break;
        case QueueAction::LIBRARY_LOAD:
            actionLibraryLoad(item.string, item.int0);
            break;
        case QueueAction::LIBRARY_SAVE:
            actionLibrarySave(item.string, item.int0);
            break;
        case QueueAction::RESET_SEQUENCE:
            actionResetSequence();
            break;
        case QueueAction::RANDOMIZE_PATCH:
            actionRandomizePatch();
            break;
        case QueueAction::NOOP:
            break;
        default:
#ifdef DEBUGMSGS
            qDebug() << "Editor::process():" << item.action << ":" << item.int0 << "," << item.int1 << "," << item.string;
#endif
            break;
    }
    emit finished();
}


void Editor::actionPatchParameterChangeEditor(int id, int value) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionPatchParameterChangeEditor(" << id << "," << value << ")";
#endif
    if (patch->getValue(id) != value) {
        patch->setValue(id, value);

        //strange hack to fix arpeggiator range
        //firmware 1.03 maps 1->1, 2->1, 3->2, 4->3
        //to circumvent this: send 1, 3, 4, 5
        if (firmwareVersion >= 1000 && id == 105 && value > 1) {
            value += 1;
        }

        if (Patch::sendAsNRPN(id)) {
            if (!midiout->nrpn(channel, id, value)) {
                emit displayStatusbar("Could not send changes as NRPN.");
            }
        } else {
            const PatchParameter &param = Patch::parameter(id, shruthiFilterBoard);
            const int &cc = param.cc;
            const int &val = 127.0 * (value - param.min) / param.max;
            if (cc >= 0) {
                if (!midiout->controlChange(channel, cc, val)) {
                    emit displayStatusbar("Could not send changes as CC.");
                }
            } else {
                emit displayStatusbar("Could not send changes.");
            }
        }
    }
}


void Editor::actionFetchRequest(const int &what) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionFetchRequest()";
#endif
    bool statusP = true;
    if (what&Flag::PATCH) {
        statusP = midiout->patchTransferRequest();
        if (statusP)
            emit displayStatusbar("Patch transfer request sent.");
        else
            emit displayStatusbar("Could not send patch transfer request.");
    }
    bool statusS = true;
    if (what&Flag::SEQUENCE) {
        statusS = midiout->sequenceTransferRequest();
        if (statusS)
            emit displayStatusbar("Sequence transfer request sent.");
        else
            emit displayStatusbar("Could not send sequence transfer request.");

    }

    QString swhat = "unknown";
    QString sWhat = "Unknown";
    QString pl = "";
    if ((what&Flag::PATCH) && (what&Flag::SEQUENCE)) {
        swhat = "patch and sequence";
        sWhat = "Patch and sequence";
        pl = "s";
    } else if ((what&Flag::PATCH) && !(what&Flag::SEQUENCE)) {
        swhat = "patch";
        sWhat = "Patch";
    } else if (!(what&Flag::PATCH) && (what&Flag::SEQUENCE)) {
        swhat = "sequence";
        sWhat = "Sequence";
    }

    if (statusP && statusS) {
        emit displayStatusbar(sWhat + " transfer request" + pl + " sent.");
    } else {
        emit displayStatusbar("Could not send " + swhat + " transfer request" + pl + ".");
    }
}


void Editor::actionSendData(const int &what) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionSendData()";
#endif
    bool statusP = true;
    if (what&Flag::PATCH) {
        Message temp;
        patch->generateSysex(&temp);
        statusP = midiout->write(temp);
    }
    bool statusS = true;
    if (what&Flag::SEQUENCE) {
        Message temp;
        sequence->generateSysex(&temp);
        statusS = midiout->write(temp);

    }

    QString swhat = "unknown";
    QString sWhat = "Unknown";
    if ((what&Flag::PATCH) && (what&Flag::SEQUENCE)) {
        swhat = "patch and sequence";
        sWhat = "Patch and sequence";
    } else if ((what&Flag::PATCH) && !(what&Flag::SEQUENCE)) {
        swhat = "patch";
        sWhat = "Patch";
    } else if (!(what&Flag::PATCH) && (what&Flag::SEQUENCE)) {
        swhat = "sequence";
        sWhat = "Sequence";
    }

    if (statusP && statusS) {
        emit displayStatusbar(sWhat + " sent.");
    } else {
        emit displayStatusbar("Could not send " + swhat + ".");
    }
}


void Editor::actionShruthiInfoRequest() {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionShruthiInfoRequest()";
#endif
    if (midiout->versionRequest() && midiout->numBanksRequest()) {
        library->setFirmwareVersionRequested();
        //emit displayStatusbar("Version request sent.");
#ifdef DEBUGMSGS
        std::cout  << "Version and number of banks requests sent." << std::endl;
    } else {
        //emit displayStatusbar("Could not send version request.");
        std::cout  << "Could not send version and/or number of banks request." << std::endl;
#endif
    }
}


void Editor::actionPatchParameterChangeMidi(int id, int value) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionPatchParameterChangeMidi(" << id << "," << value << ")";
#endif
    if (!Patch::enabled(id)) {
        return;
    }

    if (Patch::parameter(id).min < 0 && value >= 127) {
        value-=256; //2s complement
    }
    patch->setValue(id, value);
    if (Patch::hasUI(id)) {
        emit redrawPatchParameter(id, value);
    }
}


void Editor::actionNoteOn(unsigned char note, unsigned char velocity) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionNoteOn(" << channel << "," << note << "," << velocity << ")";
#endif
    if (!midiout->noteOn(channel, note, velocity)) {
        emit displayStatusbar("Could not send note on message.");
    }
}


void Editor::actionNoteOff(unsigned char note) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionNoteOff(" << channel << "," << note << ")";
#endif
    if (!midiout->noteOff(channel, note)) {
        emit displayStatusbar("Could not send note off message.");
    }
}


void Editor::actionNotePanic() {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionNotePanic(" << channel << ")";
#endif
    if (midiout->allNotesOff(channel)) {
        emit displayStatusbar("Sent all notes off message.");
    } else {
        emit displayStatusbar("Could not send all notes off message.");
    }
}


void Editor::actionSysexReceived(unsigned int command, unsigned int argument,
                                 unsigned int size, unsigned char* message) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionSysexReceived(" << size << ",...)";
#endif
    if (size == 0 && command == 0 && argument == 0) {
        emit displayStatusbar("Received invalid SysEx.");
    } else if (command == 0x0c && argument == 0x00) {
        // Version info
        if (size == 2) {
            firmwareVersion = message[0] * 1000 + message[1];
            library->setFirmwareVersion(firmwareVersion);
        }
    } else if (command == 0x0a && argument == 0x00) {
        const int &patchNo = message[0] | message[1] << 8;
        const int &sequenceNo = message[2] | message[3] << 8;
        library->rememberShruthiProgram(patchNo, sequenceNo);
#ifdef DEBUGMSGS
        qDebug() << "Current program: " << patchNo << sequenceNo;
#endif
    } else if (command == 0x01 && argument == 0x00) {
        bool ret = (size == 92);
        QString progress;
        if (ret) {
            if (library->isFetchingPatches()) {
                progress = library->fetchProgress();
                ret = library->receivedPatch(message);
                if (ret) {
                    emit redrawLibraryItems(Flag::PATCH, library->nextPatch() - 1, library->nextPatch() - 1);
                }
            } else {
                ret = patch->unpackData(message);
            }
        }

        if (ret) {
            emit displayStatusbar(progress + "Received valid patch (" + patch->getVersionString() + " format).");
            redrawAllPatchParameters();
            emit setStatusbarVersionLabel(patch->getVersionString());
        } else {
            if (library->isFetchingPatches()) {
                library->abortFetching();
            }
            emit displayStatusbar(progress + "Received invalid patch.");
        }
    } else if (command == 0x02 && argument == 0x00) {
        QString progress;
        if (size == 32) {
            if (library->isFetchingSequences()) {
                progress = library->fetchProgress();
                library->receivedSequence(message);
                emit redrawLibraryItems(Flag::SEQUENCE, library->nextSequence() - 1, library->nextSequence() - 1);
            } else {
                sequence->unpackData(message);
            }

            emit displayStatusbar(progress + "Received valid sequence.");
            redrawAllSequenceParameters();
        } else {
            if (library->isFetchingSequences()) {
                library->abortFetching();
            }
            emit displayStatusbar(progress + "Received invalid sequence.");
        }
    } else if (command == 0x0b and size == 0) {
        // number of banks
        const int &numberOfPrograms = 16 + argument * 64; // internal + external
#ifdef DEBUGMSGS
        std::cout << "Number of banks is " << argument << ". Therefore the number of programs is " << numberOfPrograms << "." << std::endl;
#endif
        library->setNumberOfHWPrograms(numberOfPrograms);
        emit redrawLibraryItems(Flag::PATCH | Flag::SEQUENCE, 0, library->getNumberOfPrograms() - 1);
    } else {
        emit displayStatusbar("Received unknown sysex.");
#ifdef DEBUGMSGS
        qDebug() << "Unknown sysex type...";
        std::cout << "Unknown sysex with command " << command << ", argument " << argument << " and length " << size << " received." << std::endl;
#endif
    }
    if (message) {
        delete message;
    }
}


void Editor::actionSetPatchname(QString name) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionSetPatchname(" << name << ")";
#endif
    patch->setName(name);
    emit displayStatusbar("Patch name set.");
}


void Editor::actionFileIOLoad(QString path, const int &what) {
    Message temp;
    bool status = FileIO::loadFromDisk(path, temp);
    bool statusP = status;
    bool statusS = status;

    const unsigned int &readBytes = temp.size();

    if (status && path.endsWith(".sp", Qt::CaseInsensitive) && (what&Flag::PATCH)) {
        if (readBytes == 92) {
#ifdef DEBUGMSGS
            qDebug() << "Detected light patch files.";
#endif
            unsigned char data[92];
            for (unsigned int i=0; i<readBytes; i++) {
                data[i] = (char) temp[i];
#ifdef DEBUGMSGS
                qDebug() << i << ":" << temp[i];
#endif
            }
            statusP = patch->unpackData(data);
        } else {
            statusP = false;
        }
    } else if (status) {
        if (what&Flag::PATCH) {
            Message ptc;
            // ignore return value; if it fails, ptc is empty:
            Midi::getPatch(&temp, &ptc);
            statusP = patch->parseSysex(&ptc);
        }
        if (what&Flag::SEQUENCE) {
            Message seq;
            // ignore return value; if it fails, seq is empty:
            Midi::getSequence(&temp, &seq);
            statusS = sequence->parseSysex(&seq);
        }
    }

#ifdef DEBUGMSGS
    qDebug() << "Editor::actionFileIOLoad(" << path << "):" << status;
#endif

    QString swhat = "unknown";
    QString sWhat = "Unknown";
    QString partial = ".";
    if ((what&Flag::PATCH) && (what&Flag::SEQUENCE)) {
        swhat = "patch and sequence";
        sWhat = "Patch and sequence";

        if (status && statusP && !statusS) {
            partial = "; only patch found.";
        }
        if (status && !statusP && statusS) {
            partial = "; only sequence found.";
        }

    } else if ((what&Flag::PATCH) && !(what&Flag::SEQUENCE)) {
        swhat = "patch";
        sWhat = "Patch";
    } else if (!(what&Flag::PATCH) && (what&Flag::SEQUENCE)) {
        swhat = "sequence";
        sWhat = "Sequence";
    }

    if (statusP && statusS) {
        emit displayStatusbar(sWhat + " loaded from disk.");
    } else {
        emit displayStatusbar("Could not load " + swhat + partial);
    }

    // Send required refresh signals
    if (statusP && (what&Flag::PATCH)) {
        redrawAllPatchParameters();
        emit setStatusbarVersionLabel(patch->getVersionString());
    }
    if (statusS && (what&Flag::SEQUENCE)) {
        redrawAllSequenceParameters();
    }
}


void Editor::actionFileIOSave(QString path, const int &what) {
    QByteArray ba;

    if (path.endsWith(".sp", Qt::CaseInsensitive)) {
        unsigned char data[92];
        patch->packData(data);
        FileIO::appendToByteArray(data, 92, ba);
    } else {
        Message temp;
        if (what&Flag::PATCH) {
            patch->generateSysex(&temp);
        }
        if (what&Flag::SEQUENCE) {
            sequence->generateSysex(&temp);
        }
        FileIO::appendToByteArray(temp, ba);
    }

    bool status = FileIO::saveToDisk(path, ba);
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionFileIOSave(" << path << "):" << status;
#endif

    QString swhat = "unknown";
    QString sWhat = "Unknown";
    if ((what&Flag::PATCH) && !(what&Flag::SEQUENCE)) {
        swhat = "patch";
        sWhat = "Patch";
    } else if (!(what&Flag::PATCH) && (what&Flag::SEQUENCE)) {
        swhat = "sequence";
        sWhat = "Sequence";
    } else if ((what&Flag::PATCH) && (what&Flag::SEQUENCE)) {
        swhat = "patch and sequence";
        sWhat = "Patch and sequence";
    }

    if (status) {
        emit displayStatusbar(sWhat + " saved to disk.");
    } else {
        emit displayStatusbar("Could not save " + swhat + ".");
    }
}


void Editor::actionResetPatch(unsigned int version) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionResetPatch()";
#endif
    patch->reset(version);
    redrawAllPatchParameters();
    emit displayStatusbar("Patch reset.");
    emit setStatusbarVersionLabel(patch->getVersionString());
}


void Editor::actionRandomizePatch() {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionRandomizePatch()";
#endif
    patch->randomize(shruthiFilterBoard);
    redrawAllPatchParameters();
    emit displayStatusbar("Patch randomized.");
    emit setStatusbarVersionLabel(patch->getVersionString());
}


void Editor::actionSequenceParameterChangeEditor(const unsigned &id, const int &value) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionSequenceParameterChangeEditor()" << id << value;
#endif
    sequence->setValueById(id, value);
}


void Editor::actionResetSequence() {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionResetSequence()";
#endif
    sequence->reset();
    redrawAllSequenceParameters();
    emit displayStatusbar("Sequence reset.");
}


void Editor::actionLibraryFetch(const unsigned int &what, const int &start, const int &stop) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionLibraryFetch()";
#endif
    if (library->isFetchingPatches() || library->isFetchingSequences()) {
        // It would be nice to enqueue several fetch commands, but it isn't that easy if you
        // consider the way fetching works. Therefore we take the easy way out.
        // If we get the command to start another fetch we either ignore it or abort fetching.
        if (stop < 0) {
            library->abortFetching();
            emit displayStatusbar("Aborted fetching the library.");
        }
        return;
    }

    bool error = !midiout->currentPatchSequenceRequest();

    const int &st = stop >= 0 ? stop : (library->getNumberOfHWPrograms() - 1);

    if (!error && library->startFetching(what, start, st)) {
        emit displayStatusbar("Started to fetch the library.");
    } else {
        emit displayStatusbar("Could not start fetching the library.");
    }
}


void Editor::actionLibrarySend(const unsigned int &what, const int &start, const int &end) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionLibrarySend()" << what << start << end;
#endif
    if (library->isSending()) {
        // It would be nice to enqueue several send commands, but it isn't that easy if you
        // consider the way fetching works. Therefore we take the easy way out.
        // If we get the command to start another send we either ignore it or abort sending.
        if (end < 0) {
            std::cout << "abort" << std::endl;
            library->abortSending();
            emit displayStatusbar("Aborted sending the library.");
        }
        return;
    }

    emit displayStatusbar("Started sending the library.");
    const int &st = end >= 0 ? end : (library->getNumberOfHWPrograms() - 1);
    actionLibrarySendReturnHandler(library->startSending(what, start, st));
}


void Editor::actionLibrarySendReturnHandler(const bool &ret) {
    if (ret && library->isSending()) {
        QTimer::singleShot(library->sendTimeout(), this, SLOT(librarySendNext()));
    }

    if (!ret) {
        emit displayStatusbar("An error occured during sending of the library.");
    }

    if (!library->isSending() && ret) {
        emit displayStatusbar("Finished sending the library.");
    }

    // Redraw UI (always do this; there could be a partial success):
    const int &index = library->sendRedrawIndex();
    if (index >= 0) {
        const int &flags = library->sendRedrawFlags();
        emit redrawLibraryItems(flags, index, index);
    }
}


void Editor::librarySendNext() {
    actionLibrarySendReturnHandler(library->keepSending());
}


void Editor::actionLibraryRecall(const unsigned int &what, const unsigned int &id) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionLibraryRecall()";
#endif
    Q_UNUSED(what);

    if (what&Flag::PATCH) {
        patch->set(library->recallPatch(id));
        redrawAllPatchParameters();
    }
    if (what&Flag::SEQUENCE) {
        sequence->set(library->recallSequence(id));
        redrawAllSequenceParameters();
    }
}


void Editor::actionLibraryStore(const unsigned int &what, const unsigned int &id) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionLibraryStore()";
#endif
    if (what&Flag::PATCH) {
        library->storePatch(id, *patch);
        redrawLibraryItems(Flag::PATCH, id, id);
    }
    if (what&Flag::SEQUENCE) {
        library->storeSequence(id, *sequence);
        redrawLibraryItems(Flag::SEQUENCE, id, id);
    }
}


void Editor::actionLibraryMove(const unsigned int &what, const unsigned int &start, const unsigned int &target) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionLibraryMove()";
#endif
    if (what&Flag::PATCH) {
        library->movePatch(start, target);
    }
    if (what&Flag::SEQUENCE) {
        library->moveSequence(start, target);
    }
    const int &s = std::min(start, target);
    const int &t = std::max(start, target);
    redrawLibraryItems(what, s, t);
}


void Editor::actionLibraryLoad(const QString &path, const int &flags) {
    // Always load patches and sequences
    if (library->loadLibrary(path, flags&Flag::APPEND)) {
        emit displayStatusbar("Library loaded from disk.");
    } else {
        emit displayStatusbar("Could not load library from disk.");
    }
    redrawLibraryItems(flags, 0, library->getNumberOfPrograms() - 1);
}


void Editor::actionLibrarySave(const QString &path, const int &flags) {
    Q_UNUSED(flags);
    // Always save patches and sequences
    if (library->saveLibrary(path)) {
        emit displayStatusbar("Library saved to disk.");
    } else {
        emit displayStatusbar("Could not save library to disk.");
    }
}


void Editor::actionLibraryRemove(const unsigned int &start, const unsigned int &end) {
#ifdef DEBUGMSGS
    qDebug() << "Editor::actionLibraryDelete()" << start << end;
#endif
    library->remove(start, end);
    redrawLibraryItems(Flag::PATCH | Flag::SEQUENCE, start, library->getNumberOfPrograms() - 1);
}


void Editor::actionLibraryInsert(const unsigned int &id) {
#ifdef DEBUGMSG
    qDebug() << "Editor::actionLibraryInsert()" << id;
#endif
    library->insert(id);
    redrawLibraryItems(Flag::PATCH | Flag::SEQUENCE, id, library->getNumberOfPrograms() - 1);
}


void Editor::actionLibraryReset(const unsigned int &flags, const unsigned int &start, const unsigned int &end) {
#ifdef DEBUGMSG
    qDebug() << "Editor::actionLibraryReset()" << flags << start << end;
#endif
    library->reset(flags, start, end);
    redrawLibraryItems(Flag::PATCH | Flag::SEQUENCE, start, end);
}


void Editor::redrawAllPatchParameters() {
    for (int i = 0; i < 110; i++) {
        if (Patch::hasUI(i) || Patch::hasUI2(i)) {
            emit redrawPatchParameter(i, patch->getValue(i));
        }
    }
    emit redrawPatchName(patch->getName());
}


void Editor::redrawAllSequenceParameters() {
    for (int i = 0; i < Sequence::NUMBER_OF_STEPS; i++) {
        const int &active = sequence->getValue(i, SequenceParameter::ACTIVE);
        const int &note = sequence->getValue(i, SequenceParameter::NOTE);
        const int &tie = sequence->getValue(i, SequenceParameter::TIE);
        const int &velocity = sequence->getValue(i, SequenceParameter::VELOCITY);
        const int &value = sequence->getValue(i, SequenceParameter::VALUE);
        emit redrawSequenceStep(i, active, note, tie, velocity, value);
    }
}


void Editor::redrawLibraryItems(int what, int start, int stop) {
    emit setNumberOfLibraryPrograms(library->getNumberOfPrograms());
    for (int i = start; i <= stop; i++) {
        const bool &onHardware = i < library->getNumberOfHWPrograms();
        if (what&Flag::PATCH) {
            const QString &name = library->getPatchIdentifier(i);
            const bool &edited = library->patchEdited(i);
            const bool &moved = library->patchMoved(i);
            emit redrawLibraryPatchItem(i, name, edited, moved, onHardware);
        }
        if (what&Flag::SEQUENCE) {
            const QString &name = library->getSequenceIdentifier(i);
            const bool &edited = library->patchEdited(i);
            const bool &moved = library->patchMoved(i);
            emit redrawLibrarySequenceItem(i, name, edited, moved, onHardware);
        }
    }
}
