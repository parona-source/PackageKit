/*
 * Copyright (c) 2012 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2012 Matthias Klumpp <matthias@tenstral.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "AptCacheFile.h"

#include <apt-pkg/algorithms.h>
#include <sstream>
#include <cstdio>

AptCacheFile::AptCacheFile(PkBackend *backend) :
    m_packageRecords(0)
{
    std::cout << "AptCacheFile::AptCacheFile()" << std::endl;
}

AptCacheFile::~AptCacheFile()
{
    std::cout << "AptCacheFile::~AptCacheFile()" << std::endl;

    delete m_packageRecords;
    pkgCacheFile::Close();
}

bool AptCacheFile::Open(bool withLock)
{
    return pkgCacheFile::Open(NULL, withLock);
}

void AptCacheFile::Close()
{
    if (m_packageRecords) {
        delete m_packageRecords;
    }
    m_packageRecords = 0;

    pkgCacheFile::Close();
}

bool AptCacheFile::BuildCaches(bool withLock)
{
    return pkgCacheFile::BuildCaches(NULL, withLock);
}

bool AptCacheFile::CheckDeps(bool AllowBroken)
{
    bool FixBroken = _config->FindB("APT::Get::Fix-Broken",false);

    if (_error->PendingError() == true) {
        return false;
    }

    // Check that the system is OK
    if (DCache->DelCount() != 0 || DCache->InstCount() != 0) {
        return _error->Error("Internal error, non-zero counts");
    }

    // Apply corrections for half-installed packages
    if (pkgApplyStatus(*DCache) == false) {
        return false;
    }

    if (_config->FindB("APT::Get::Fix-Policy-Broken",false) == true) {
        FixBroken = true;
        if ((DCache->PolicyBrokenCount() > 0)) {
            // upgrade all policy-broken packages with ForceImportantDeps=True
            for (pkgCache::PkgIterator I = Cache->PkgBegin(); !I.end(); ++I) {
                if ((*DCache)[I].NowPolicyBroken() == true) {
                    DCache->MarkInstall(I,true,0, false, true);
                }
            }
        }
    }

    // Nothing is broken
    if (DCache->BrokenCount() == 0 || AllowBroken == true) {
        return true;
    }

    // Attempt to fix broken things
    if (FixBroken == true) {
//        c1out << _("Correcting dependencies...") << flush;
        if (pkgFixBroken(*DCache) == false || DCache->BrokenCount() != 0) {
//            c1out << _(" failed.") << endl;
            ShowBroken(true);

            return _error->Error("Unable to correct dependencies");
        }
        if (pkgMinimizeUpgrade(*DCache) == false) {
            return _error->Error("Unable to minimize the upgrade set");
        }

//        c1out << _(" Done") << endl;
    } else {
//        c1out << _("You might want to run 'apt-get -f install' to correct these.") << endl;
        ShowBroken(true);

        return _error->Error("Unmet dependencies. Try using -f.");
    }

    return true;
}

void AptCacheFile::ShowBroken(bool Now)
{
    std::stringstream out;

    out << "The following packages have unmet dependencies:" << std::endl;
    for (pkgCache::PkgIterator I = (*this)->PkgBegin(); ! I.end(); ++I) {
        if (Now == true) {
            if ((*this)[I].NowBroken() == false) {
                continue;
            }
        } else {
            if ((*this)[I].InstBroken() == false){
                continue;
            }
        }

        // Print out each package and the failed dependencies
        out << "  " <<  I.Name() << ":";
        unsigned Indent = strlen(I.Name()) + 3;
        bool First = true;
        pkgCache::VerIterator Ver;

        if (Now == true) {
            Ver = I.CurrentVer();
        } else {
            Ver = (*this)[I].InstVerIter(*this);
        }

        if (Ver.end() == true) {
            out << std::endl;
            continue;
        }

        for (pkgCache::DepIterator D = Ver.DependsList(); D.end() == false;) {
            // Compute a single dependency element (glob or)
            pkgCache::DepIterator Start;
            pkgCache::DepIterator End;
            D.GlobOr(Start,End); // advances D

            if ((*this)->IsImportantDep(End) == false){
                continue;
            }

            if (Now == true) {
                if (((*this)[End] & pkgDepCache::DepGNow) == pkgDepCache::DepGNow){
                    continue;
                }
            } else {
                if (((*this)[End] & pkgDepCache::DepGInstall) == pkgDepCache::DepGInstall) {
                    continue;
                }
            }

            bool FirstOr = true;
            while (1) {
                if (First == false){
                    for (unsigned J = 0; J != Indent; J++) {
                        out << ' ';
                    }
                }
                First = false;

                if (FirstOr == false) {
                    for (unsigned J = 0; J != strlen(End.DepType()) + 3; J++) {
                        out << ' ';
                    }
                } else {
                    out << ' ' << End.DepType() << ": ";
                }
                FirstOr = false;

                out << Start.TargetPkg().Name();

                // Show a quick summary of the version requirements
                if (Start.TargetVer() != 0) {
                    out << " (" << Start.CompType() << " " << Start.TargetVer() << ")";
                }

                /* Show a summary of the target package if possible. In the case
                of virtual packages we show nothing */
                pkgCache::PkgIterator Targ = Start.TargetPkg();
                if (Targ->ProvidesList == 0) {
                    out << ' ';
                    pkgCache::VerIterator Ver = (*this)[Targ].InstVerIter(*this);
                    if (Now == true) {
                        Ver = Targ.CurrentVer();
                    }

                    if (Ver.end() == false)
                    {
                        char buffer[1024];
                        if (Now == true) {
                            sprintf(buffer, "but %s is installed", Ver.VerStr());
                        } else {
                            sprintf(buffer, "but %s is to be installed", Ver.VerStr());
                        }

                        out << buffer;
                    } else {
                        if ((*this)[Targ].CandidateVerIter(*this).end() == true) {
                            if (Targ->ProvidesList == 0) {
                                out << "but it is not installable";
                            } else {
                                out << "but it is a virtual package";
                            }
                        } else {
                            if (Now) {
                                out << "but it is not installed";
                            } else {
                                out << "but it is not going to be installed";
                            }
                        }
                    }
                }

                if (Start != End) {
                    out << " or";
                }
                out << std::endl;

                if (Start == End){
                    break;
                }
                Start++;
            }
        }
    }
    pk_backend_error_code(m_backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, out.str().c_str());
}

void AptCacheFile::buildPkgRecords()
{
    if (m_packageRecords) {
        return;
    }

    // Create the text record parser
    m_packageRecords = new pkgRecords(*this);
}
