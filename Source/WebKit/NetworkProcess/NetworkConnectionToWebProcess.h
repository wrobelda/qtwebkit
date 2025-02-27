/*
 * Copyright (C) 2012-2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "CacheStorageEngineConnection.h"
#include "Connection.h"
#include "DownloadID.h"
#include "NetworkActivityTracker.h"
#include "NetworkConnectionToWebProcessMessages.h"
#include "NetworkMDNSRegister.h"
#include "NetworkRTCProvider.h"
#include "NetworkResourceLoadMap.h"
#include "WebPaymentCoordinatorProxy.h"
#include "WebResourceLoadObserver.h"
#include <WebCore/FrameIdentifier.h>
#include <WebCore/MessagePortChannelProvider.h>
#include <WebCore/MessagePortIdentifier.h>
#include <WebCore/NetworkLoadInformation.h>
#include <WebCore/PageIdentifier.h>
#include <WebCore/ProcessIdentifier.h>
#include <WebCore/RegistrableDomain.h>
#include <wtf/RefCounted.h>

namespace PAL {
class SessionID;
}

namespace WebCore {
class BlobDataFileReference;
class BlobRegistryImpl;
class ResourceError;
class ResourceRequest;
enum class StorageAccessPromptWasShown : bool;
enum class StorageAccessWasGranted : bool;
struct SameSiteInfo;

enum class IncludeSecureCookies : bool;
}

namespace WebKit {

class NetworkProcess;
class NetworkResourceLoader;
class NetworkSession;
class NetworkSocketChannel;
class NetworkSocketStream;
class WebIDBConnectionToClient;
class WebSWServerConnection;
class WebSWServerToContextConnection;
typedef uint64_t ResourceLoadIdentifier;

namespace NetworkCache {
struct DataKey;
}

class NetworkConnectionToWebProcess
    : public RefCounted<NetworkConnectionToWebProcess>
#if ENABLE(APPLE_PAY_REMOTE_UI)
    , public WebPaymentCoordinatorProxy::Client
#endif
    , IPC::Connection::Client {
public:
    using RegistrableDomain = WebCore::RegistrableDomain;

    static Ref<NetworkConnectionToWebProcess> create(NetworkProcess&, WebCore::ProcessIdentifier, PAL::SessionID, IPC::Connection::Identifier);
    virtual ~NetworkConnectionToWebProcess();
    
    PAL::SessionID sessionID() const { return m_sessionID; }
    NetworkSession* networkSession();

    IPC::Connection& connection() { return m_connection.get(); }
    NetworkProcess& networkProcess() { return m_networkProcess.get(); }

    void didCleanupResourceLoader(NetworkResourceLoader&);
    void transferKeptAliveLoad(NetworkResourceLoader&);
    void setOnLineState(bool);

    bool captureExtraNetworkLoadMetricsEnabled() const { return m_captureExtraNetworkLoadMetricsEnabled; }

    RefPtr<WebCore::BlobDataFileReference> getBlobDataFileReferenceForPath(const String& path);

    void cleanupForSuspension(Function<void()>&&);
    void endSuspension();

    void getNetworkLoadInformationRequest(ResourceLoadIdentifier identifier, CompletionHandler<void(const WebCore::ResourceRequest&)>&& completionHandler)
    {
        completionHandler(m_networkLoadInformationByID.get(identifier).request);
    }

    void getNetworkLoadInformationResponse(ResourceLoadIdentifier identifier, CompletionHandler<void(const WebCore::ResourceResponse&)>&& completionHandler)
    {
        completionHandler(m_networkLoadInformationByID.get(identifier).response);
    }

    void getNetworkLoadIntermediateInformation(ResourceLoadIdentifier identifier, CompletionHandler<void(const Vector<WebCore::NetworkTransactionInformation>&)>&& completionHandler)
    {
        completionHandler(m_networkLoadInformationByID.get(identifier).transactions);
    }

    void takeNetworkLoadInformationMetrics(ResourceLoadIdentifier identifier, CompletionHandler<void(const WebCore::NetworkLoadMetrics&)>&& completionHandler)
    {
        completionHandler(m_networkLoadInformationByID.take(identifier).metrics);
    }

    void addNetworkLoadInformation(ResourceLoadIdentifier identifier, WebCore::NetworkLoadInformation&& information)
    {
        ASSERT(!m_networkLoadInformationByID.contains(identifier));
        m_networkLoadInformationByID.add(identifier, WTFMove(information));
    }

    void addNetworkLoadInformationMetrics(ResourceLoadIdentifier identifier, const WebCore::NetworkLoadMetrics& metrics)
    {
        ASSERT(m_networkLoadInformationByID.contains(identifier));
        m_networkLoadInformationByID.ensure(identifier, [] {
            return WebCore::NetworkLoadInformation { };
        }).iterator->value.metrics = metrics;
    }

    void removeNetworkLoadInformation(ResourceLoadIdentifier identifier)
    {
        m_networkLoadInformationByID.remove(identifier);
    }

    Optional<NetworkActivityTracker> startTrackingResourceLoad(WebCore::PageIdentifier, ResourceLoadIdentifier resourceID, bool isTopResource);
    void stopTrackingResourceLoad(ResourceLoadIdentifier resourceID, NetworkActivityTracker::CompletionCode);

    Vector<RefPtr<WebCore::BlobDataFileReference>> resolveBlobReferences(const NetworkResourceLoadParameters&);

    void removeSocketChannel(uint64_t identifier);

    WebCore::ProcessIdentifier webProcessIdentifier() const { return m_webProcessIdentifier; }

    void checkProcessLocalPortForActivity(const WebCore::MessagePortIdentifier&, CompletionHandler<void(WebCore::MessagePortChannelProvider::HasActivity)>&&);

#if ENABLE(SERVICE_WORKER)
    void serverToContextConnectionNoLongerNeeded();
#endif

private:
    NetworkConnectionToWebProcess(NetworkProcess&, WebCore::ProcessIdentifier, PAL::SessionID, IPC::Connection::Identifier);

    void didFinishPreconnection(uint64_t preconnectionIdentifier, const WebCore::ResourceError&);
    WebCore::NetworkStorageSession* storageSession();

    // IPC::Connection::Client
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) override;
    void didReceiveSyncMessage(IPC::Connection&, IPC::Decoder&, std::unique_ptr<IPC::Encoder>&) override;
    void didClose(IPC::Connection&) override;
    void didReceiveInvalidMessage(IPC::Connection&, IPC::StringReference messageReceiverName, IPC::StringReference messageName) override;

    // Message handlers.
    void didReceiveNetworkConnectionToWebProcessMessage(IPC::Connection&, IPC::Decoder&);
    void didReceiveSyncNetworkConnectionToWebProcessMessage(IPC::Connection&, IPC::Decoder&, std::unique_ptr<IPC::Encoder>&);

    void scheduleResourceLoad(NetworkResourceLoadParameters&&);
    void performSynchronousLoad(NetworkResourceLoadParameters&&, Messages::NetworkConnectionToWebProcess::PerformSynchronousLoad::DelayedReply&&);
    void testProcessIncomingSyncMessagesWhenWaitingForSyncReply(WebPageProxyIdentifier, Messages::NetworkConnectionToWebProcess::TestProcessIncomingSyncMessagesWhenWaitingForSyncReply::DelayedReply&&);
    void loadPing(NetworkResourceLoadParameters&&);
    void prefetchDNS(const String&);
    void preconnectTo(uint64_t preconnectionIdentifier, NetworkResourceLoadParameters&&);

    void removeLoadIdentifier(ResourceLoadIdentifier);
    void pageLoadCompleted(WebCore::PageIdentifier);
    void crossOriginRedirectReceived(ResourceLoadIdentifier, const URL& redirectURL);
    void startDownload(DownloadID, const WebCore::ResourceRequest&, const String& suggestedName = { });
    void convertMainResourceLoadToDownload(uint64_t mainResourceLoadIdentifier, DownloadID, const WebCore::ResourceRequest&, const WebCore::ResourceResponse&);

    void cookiesForDOM(const URL& firstParty, const WebCore::SameSiteInfo&, const URL&, Optional<WebCore::FrameIdentifier>, Optional<WebCore::PageIdentifier>, WebCore::IncludeSecureCookies, CompletionHandler<void(String cookieString, bool secureCookiesAccessed)>&&);
    void setCookiesFromDOM(const URL& firstParty, const WebCore::SameSiteInfo&, const URL&, Optional<WebCore::FrameIdentifier>, Optional<WebCore::PageIdentifier>, const String&);
    void cookiesEnabled(CompletionHandler<void(bool)>&&);
    void cookieRequestHeaderFieldValue(const URL& firstParty, const WebCore::SameSiteInfo&, const URL&, Optional<WebCore::FrameIdentifier>, Optional<WebCore::PageIdentifier>, WebCore::IncludeSecureCookies, CompletionHandler<void(String cookieString, bool secureCookiesAccessed)>&&);
    void getRawCookies(const URL& firstParty, const WebCore::SameSiteInfo&, const URL&, Optional<WebCore::FrameIdentifier>, Optional<WebCore::PageIdentifier>, CompletionHandler<void(Vector<WebCore::Cookie>&&)>&&);
    void deleteCookie(const URL&, const String& cookieName);

    void registerFileBlobURL(const URL&, const String& path, SandboxExtension::Handle&&, const String& contentType);
    void registerBlobURL(const URL&, Vector<WebCore::BlobPart>&&, const String& contentType);
    void registerBlobURLFromURL(const URL&, const URL& srcURL);
    void registerBlobURLOptionallyFileBacked(const URL&, const URL& srcURL, const String& fileBackedPath, const String& contentType);
    void registerBlobURLForSlice(const URL&, const URL& srcURL, int64_t start, int64_t end);
    void blobSize(const URL&, CompletionHandler<void(uint64_t)>&&);
    void unregisterBlobURL(const URL&);
    void writeBlobsToTemporaryFiles(const Vector<String>& blobURLs, CompletionHandler<void(Vector<String>&&)>&&);

    void setCaptureExtraNetworkLoadMetricsEnabled(bool);

    void createSocketStream(URL&&, String cachePartition, uint64_t);

    void createSocketChannel(const WebCore::ResourceRequest&, const String& protocol, uint64_t identifier);

#if ENABLE(INDEXED_DATABASE)
    // Messages handlers (Modern IDB).
    void establishIDBConnectionToServer();
#endif

#if ENABLE(SERVICE_WORKER)
    void establishSWServerConnection();
    void establishSWContextConnection(WebCore::RegistrableDomain&&);
    void closeSWContextConnection();
    void unregisterSWConnection();
#endif

    void createNewMessagePortChannel(const WebCore::MessagePortIdentifier& port1, const WebCore::MessagePortIdentifier& port2);
    void entangleLocalPortInThisProcessToRemote(const WebCore::MessagePortIdentifier& local, const WebCore::MessagePortIdentifier& remote);
    void messagePortDisentangled(const WebCore::MessagePortIdentifier&);
    void messagePortClosed(const WebCore::MessagePortIdentifier&);
    void takeAllMessagesForPort(const WebCore::MessagePortIdentifier&, CompletionHandler<void(Vector<WebCore::MessageWithMessagePorts>&&, uint64_t)>&&);
    void postMessageToRemote(WebCore::MessageWithMessagePorts&&, const WebCore::MessagePortIdentifier&);
    void checkRemotePortForActivity(const WebCore::MessagePortIdentifier, CompletionHandler<void(bool)>&&);
    void didDeliverMessagePortMessages(uint64_t messageBatchIdentifier);

#if USE(LIBWEBRTC)
    NetworkRTCProvider& rtcProvider();
#endif
#if ENABLE(WEB_RTC)
    NetworkMDNSRegister& mdnsRegister() { return m_mdnsRegister; }
#endif

    CacheStorageEngineConnection& cacheStorageConnection();

#if ENABLE(RESOURCE_LOAD_STATISTICS)
    void removeStorageAccessForFrame(WebCore::FrameIdentifier, WebCore::PageIdentifier);
    void clearPageSpecificDataForResourceLoadStatistics(WebCore::PageIdentifier);

    void logUserInteraction(const RegistrableDomain&);
    void resourceLoadStatisticsUpdated(Vector<WebCore::ResourceLoadStatistics>&&);
    void hasStorageAccess(const RegistrableDomain& subFrameDomain, const RegistrableDomain& topFrameDomain, WebCore::FrameIdentifier, WebCore::PageIdentifier, CompletionHandler<void(bool)>&&);
    void requestStorageAccess(const RegistrableDomain& subFrameDomain, const RegistrableDomain& topFrameDomain, WebCore::FrameIdentifier, WebCore::PageIdentifier, WebPageProxyIdentifier, CompletionHandler<void(WebCore::StorageAccessWasGranted, WebCore::StorageAccessPromptWasShown)>&&);
    void requestStorageAccessUnderOpener(WebCore::RegistrableDomain&& domainInNeedOfStorageAccess, WebCore::PageIdentifier openerPageID, WebCore::RegistrableDomain&& openerDomain);
#endif

    void addOriginAccessWhitelistEntry(const String& sourceOrigin, const String& destinationProtocol, const String& destinationHost, bool allowDestinationSubdomains);
    void removeOriginAccessWhitelistEntry(const String& sourceOrigin, const String& destinationProtocol, const String& destinationHost, bool allowDestinationSubdomains);
    void resetOriginAccessWhitelists();

    uint64_t nextMessageBatchIdentifier(Function<void()>&&);

    struct ResourceNetworkActivityTracker {
        ResourceNetworkActivityTracker() = default;
        ResourceNetworkActivityTracker(const ResourceNetworkActivityTracker&) = default;
        ResourceNetworkActivityTracker(ResourceNetworkActivityTracker&&) = default;
        ResourceNetworkActivityTracker(WebCore::PageIdentifier pageID)
            : pageID { pageID }
            , isRootActivity { true }
            , networkActivity { NetworkActivityTracker::Label::LoadPage }
        {
        }

        ResourceNetworkActivityTracker(WebCore::PageIdentifier pageID, ResourceLoadIdentifier resourceID)
            : pageID { pageID }
            , resourceID { resourceID }
            , networkActivity { NetworkActivityTracker::Label::LoadResource }
        {
        }

        WebCore::PageIdentifier pageID;
        ResourceLoadIdentifier resourceID { 0 };
        bool isRootActivity { false };
        NetworkActivityTracker networkActivity;
    };

    void stopAllNetworkActivityTracking();
    void stopAllNetworkActivityTrackingForPage(WebCore::PageIdentifier);
    size_t findRootNetworkActivity(WebCore::PageIdentifier);
    size_t findNetworkActivityTracker(ResourceLoadIdentifier resourceID);

#if ENABLE(APPLE_PAY_REMOTE_UI)
    WebPaymentCoordinatorProxy& paymentCoordinator();

    // WebPaymentCoordinatorProxy::Client
    IPC::Connection* paymentCoordinatorConnection(const WebPaymentCoordinatorProxy&) final;
    UIViewController *paymentCoordinatorPresentingViewController(const WebPaymentCoordinatorProxy&) final;
    const String& paymentCoordinatorBoundInterfaceIdentifier(const WebPaymentCoordinatorProxy&) final;
    const String& paymentCoordinatorCTDataConnectionServiceType(const WebPaymentCoordinatorProxy&) final;
    const String& paymentCoordinatorSourceApplicationBundleIdentifier(const WebPaymentCoordinatorProxy&) final;
    const String& paymentCoordinatorSourceApplicationSecondaryIdentifier(const WebPaymentCoordinatorProxy&) final;
    std::unique_ptr<PaymentAuthorizationPresenter> paymentCoordinatorAuthorizationPresenter(WebPaymentCoordinatorProxy&, PKPaymentRequest *) final;
    void paymentCoordinatorAddMessageReceiver(WebPaymentCoordinatorProxy&, const IPC::StringReference&, IPC::MessageReceiver&) final;
    void paymentCoordinatorRemoveMessageReceiver(WebPaymentCoordinatorProxy&, const IPC::StringReference&) final;
#endif

    Ref<IPC::Connection> m_connection;
    Ref<NetworkProcess> m_networkProcess;
    PAL::SessionID m_sessionID;

    HashMap<uint64_t, RefPtr<NetworkSocketStream>> m_networkSocketStreams;
    HashMap<uint64_t, std::unique_ptr<NetworkSocketChannel>> m_networkSocketChannels;
    NetworkResourceLoadMap m_networkResourceLoaders;
    HashMap<String, RefPtr<WebCore::BlobDataFileReference>> m_blobDataFileReferences;
    Vector<ResourceNetworkActivityTracker> m_networkActivityTrackers;

    HashMap<ResourceLoadIdentifier, WebCore::NetworkLoadInformation> m_networkLoadInformationByID;


#if USE(LIBWEBRTC)
    RefPtr<NetworkRTCProvider> m_rtcProvider;
#endif
#if ENABLE(WEB_RTC)
    NetworkMDNSRegister m_mdnsRegister;
#endif

    bool m_captureExtraNetworkLoadMetricsEnabled { false };

    RefPtr<CacheStorageEngineConnection> m_cacheStorageConnection;

#if ENABLE(INDEXED_DATABASE)
    std::unique_ptr<WebIDBConnectionToClient> m_webIDBConnection;
#endif

#if ENABLE(SERVICE_WORKER)
    WeakPtr<WebSWServerConnection> m_swConnection;
    std::unique_ptr<WebSWServerToContextConnection> m_swContextConnection;
#endif

#if ENABLE(APPLE_PAY_REMOTE_UI)
    std::unique_ptr<WebPaymentCoordinatorProxy> m_paymentCoordinator;
#endif
    const WebCore::ProcessIdentifier m_webProcessIdentifier;

    HashSet<WebCore::MessagePortIdentifier> m_processEntangledPorts;
    HashMap<uint64_t, Function<void()>> m_messageBatchDeliveryCompletionHandlers;
};

} // namespace WebKit
