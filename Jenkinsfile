pipeline
{
    agent any
    stages
    {
        stage('build')
        {
            steps
            {
                sh 'make'
            }
        }
    }
    post
    {
        always
        {
            deleteDir() /* clean up our workspace */
        }
        success
        {
           slackstatus("success")
        }
        failure
        {
            slackstatus("fail")
        }
    }
}
